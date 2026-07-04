/* term.c — terminal core: PTY + libvterm + per color-run LVGL labels.
 * SGR: foreground + background colors, reverse-video (fg/bg swap) and underline.
 * Default is green-on-black to match the native console. */
#include "term.h"
#include "pty.h"
#include "logsink.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vterm.h>

#define MAX_ROWS 40
#define MAX_COLS 128
#define MAX_RUNS 64          /* style runs per row (split on fg/bg/underline);
                                >= max columns at supported font sizes (45@12px) */
#define COL_FG   0x4CD96A    /* default fg (green, matches native console) */
#define SB_CAP   400         /* scrollback lines kept */

typedef struct { uint32_t cp[MAX_COLS]; uint32_t rgb[MAX_COLS]; short len; } sbline_t;

static struct {
    VTerm        *vt;
    VTermScreen  *vts;
    VTermState   *state;
    pty_t         pty;
    pthread_t     reader;
    pthread_mutex_t mtx;
    volatile int  running;
    volatile int  dirty;
    volatile int  alive;
    atomic_uint   rx_seq;       /* bumped on every PTY read (output-activity clock; cross-thread) */
    int           inited;      /* resources live (mutex/vt/reader) — destroy guard */
    int           paused;      /* freeze rendering while an overlay covers the term */
    sbline_t     *sb;          /* scrollback ring */
    int           sb_n, sb_head, scroll;   /* count, oldest slot, view offset (0=live) */
    int           cols, rows, cell_w, cell_h;
    const lv_font_t *font;
    lv_obj_t     *runlbl[MAX_ROWS][MAX_RUNS];   /* per color-run labels */
    int           runs[MAX_ROWS];
    lv_obj_t     *cursor;
    lv_obj_t     *parent;
    lv_timer_t   *timer;
    const char   *test;        /* TERM_TEST env (headless input self-test) */
    lv_timer_t   *test_timer;
} g;

/* ---- utf8 ---- */
static int cp_to_utf8(uint32_t cp, char *o)
{
    if (cp < 0x80)      { o[0] = (char)cp; return 1; }
    if (cp < 0x800)     { o[0] = 0xC0 | (cp >> 6);  o[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000)   { o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F); o[2] = 0x80 | (cp & 0x3F); return 3; }
    o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F);
    o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F); return 4;
}

/* libvterm output (DA/DSR replies etc.) -> PTY */
static void out_cb(const char *s, size_t len, void *user)
{
    (void)user;
    pty_write(&g.pty, s, len);
}

/* reader thread: PTY bytes -> libvterm */
static void *reader_fn(void *arg)
{
    (void)arg;
    char buf[4096];
    while (g.running) {
        int n = pty_read(&g.pty, buf, sizeof(buf));
        if (n <= 0) { g.alive = 0; break; }
        logsink_write(buf, n);            /* raw session log (no-op if logging off) */
        pthread_mutex_lock(&g.mtx);
        vterm_input_write(g.vt, buf, (size_t)n);
        g.dirty = 1;
        atomic_fetch_add_explicit(&g.rx_seq, 1, memory_order_relaxed);  /* sendfile pacing clock */
        pthread_mutex_unlock(&g.mtx);
    }
    return NULL;
}

static uint32_t cell_rgb(VTermScreenCell *cell)
{
    VTermColor c = cell->fg;
    vterm_screen_convert_color_to_rgb(g.vts, &c);
    return ((uint32_t)c.rgb.red << 16) | ((uint32_t)c.rgb.green << 8) | c.rgb.blue;
}

/* fg/bg (as RGB) + underline for a live cell: bold brightens the foreground
 * ("bold as bright", no bold face needed), then reverse-video swaps fg/bg. */
static void cell_colors(VTermScreenCell *cell, uint32_t *fg, uint32_t *bg, int *ul)
{
    VTermColor f = cell->fg, b = cell->bg;
    vterm_screen_convert_color_to_rgb(g.vts, &f);
    vterm_screen_convert_color_to_rgb(g.vts, &b);
    uint32_t frgb = ((uint32_t)f.rgb.red << 16) | ((uint32_t)f.rgb.green << 8) | f.rgb.blue;
    uint32_t brgb = ((uint32_t)b.rgb.red << 16) | ((uint32_t)b.rgb.green << 8) | b.rgb.blue;
    if (cell->attrs.bold) {
        uint32_t r = (frgb >> 16) & 0xFF, gg = (frgb >> 8) & 0xFF, bl = frgb & 0xFF;
        r += (255 - r) / 3; gg += (255 - gg) / 3; bl += (255 - bl) / 3;
        frgb = (r << 16) | (gg << 8) | bl;
    }
    if (cell->attrs.reverse) { uint32_t t = frgb; frgb = brgb; brgb = t; }
    *fg = frgb; *bg = brgb; *ul = cell->attrs.underline ? 1 : 0;
}

/* a line scrolled off the top -> store it in the scrollback ring */
static int sb_push(int cols, const VTermScreenCell *cells, void *user)
{
    (void)user;
    if (!g.sb) return 0;
    int slot;
    if (g.sb_n < SB_CAP) { slot = (g.sb_head + g.sb_n) % SB_CAP; g.sb_n++; }
    else                 { slot = g.sb_head; g.sb_head = (g.sb_head + 1) % SB_CAP; }
    sbline_t *L = &g.sb[slot];
    int n = cols < MAX_COLS ? cols : MAX_COLS;
    L->len = (short)n;
    for (int c = 0; c < n; c++) {
        uint32_t cp = cells[c].chars[0]; if (cp == 0 || cp == (uint32_t)-1) cp = ' ';
        L->cp[c] = cp;
        VTermColor col = cells[c].fg;
        vterm_screen_convert_color_to_rgb(g.vts, &col);
        L->rgb[c] = ((uint32_t)col.rgb.red << 16) | ((uint32_t)col.rgb.green << 8) | col.rgb.blue;
    }
    if (g.scroll > 0 && g.scroll < g.sb_n) g.scroll++;   /* keep the view stable while scrolled */
    return 1;
}
static const VTermScreenCallbacks SCB = { .sb_pushline = sb_push };

void term_scroll(int delta)   /* +up (into history), -down (toward live) */
{
    pthread_mutex_lock(&g.mtx);
    g.scroll += delta;
    if (g.scroll < 0) g.scroll = 0;
    if (g.scroll > g.sb_n) g.scroll = g.sb_n;
    g.dirty = 1;
    pthread_mutex_unlock(&g.mtx);
}
void term_scroll_reset(void) { pthread_mutex_lock(&g.mtx); g.scroll = 0; g.dirty = 1; pthread_mutex_unlock(&g.mtx); }
int  term_scroll_pos(void)   { return g.scroll; }

/* Copy one on-screen content line (screen_row 0..rows-1, honoring the current
 * scroll offset) as UTF-8 into out; trailing blanks trimmed. Returns length. */
int term_copy_line(int screen_row, char *out, size_t outsz)
{
    if (!g.vt || !out || outsz == 0) return 0;
    size_t li = 0;
    pthread_mutex_lock(&g.mtx);
    int idx = g.sb_n - g.scroll + screen_row;
    if (idx >= g.sb_n) {                          /* live screen row */
        int row = idx - g.sb_n;
        for (int c = 0; row < g.rows && c < g.cols; ) {
            VTermPos pos; pos.row = row; pos.col = c;
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(g.vts, pos, &cell)) { c++; continue; }
            uint32_t cp = cell.chars[0]; if (cp == 0 || cp == (uint32_t)-1) cp = ' ';
            if (li + 4 < outsz) li += cp_to_utf8(cp, out + li);
            c += cell.width > 0 ? cell.width : 1;
        }
    } else if (idx >= 0 && g.sb) {                /* scrollback history line */
        sbline_t *L = &g.sb[(g.sb_head + idx) % SB_CAP];
        for (int c = 0; c < L->len; c++) {
            uint32_t cp = L->cp[c] ? L->cp[c] : ' ';
            if (li + 4 < outsz) li += cp_to_utf8(cp, out + li);
        }
    }
    pthread_mutex_unlock(&g.mtx);
    out[li] = 0;
    while (li > 0 && out[li - 1] == ' ') out[--li] = 0;   /* trim trailing blanks */
    return (int)li;
}

static void clear_runs(int r)
{
    for (int i = 0; i < g.runs[r]; i++)
        if (g.runlbl[r][i]) { lv_obj_delete(g.runlbl[r][i]); g.runlbl[r][i] = NULL; }
    g.runs[r] = 0;
}

static lv_obj_t *mkrun(int r, int col, uint32_t fg, uint32_t bg, int ul, const char *txt)
{
    lv_obj_t *l = lv_label_create(g.parent);
    lv_obj_set_style_text_font(l, g.font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_text_letter_space(l, 0, 0);
    lv_obj_set_style_text_line_space(l, 0, 0);
    if (bg != 0x000000) {                       /* non-default bg (incl. reverse-video) */
        lv_obj_set_style_bg_color(l, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    if (ul) lv_obj_set_style_text_decor(l, LV_TEXT_DECOR_UNDERLINE, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(l, col * g.cell_w, r * g.cell_h);
    lv_label_set_text(l, txt);
    return l;
}

/* render: cells -> per color-run labels (LVGL thread) */
void term_render_pause(int paused) { g.paused = paused ? 1 : 0; }

static void do_render(void)
{
    pthread_mutex_lock(&g.mtx);
    g.dirty = 0;

    char run[MAX_COLS * 4 + 1];
    for (int r = 0; r < g.rows; r++) {
        clear_runs(r);
        int idx = g.sb_n - g.scroll + r;            /* content line: <sb_n = history */
        int live = idx >= g.sb_n;
        sbline_t *L = live ? NULL : &g.sb[(g.sb_head + idx) % SB_CAP];
        int started = 0, rstart = 0, li = 0, rul = 0;
        uint32_t rfg = 0, rbg = 0;
        for (int c = 0; c < g.cols; ) {
            uint32_t cp, fg, bg = 0x000000; int ul = 0, w = 1;
            if (live) {
                VTermPos pos; pos.row = idx - g.sb_n; pos.col = c;
                VTermScreenCell cell;
                if (!vterm_screen_get_cell(g.vts, pos, &cell)) { c++; continue; }
                cp = cell.chars[0]; if (cp == 0 || cp == (uint32_t)-1) cp = ' ';
                cell_colors(&cell, &fg, &bg, &ul); w = cell.width > 0 ? cell.width : 1;
            } else {   /* scrollback history keeps fg only (plain scrolled-off text) */
                if (L && c < L->len) { cp = L->cp[c]; fg = L->rgb[c]; }
                else { cp = ' '; fg = COL_FG; }
            }
            if (!started) { started = 1; rstart = c; rfg = fg; rbg = bg; rul = ul; li = 0; }
            else if (fg != rfg || bg != rbg || ul != rul) {
                run[li] = 0;
                if (g.runs[r] < MAX_RUNS) g.runlbl[r][g.runs[r]++] = mkrun(r, rstart, rfg, rbg, rul, run);
                rstart = c; rfg = fg; rbg = bg; rul = ul; li = 0;
            }
            if (li < MAX_COLS * 4 - 4) li += cp_to_utf8(cp, run + li);
            c += w;
        }
        if (started) {
            run[li] = 0;
            if (g.runs[r] < MAX_RUNS) g.runlbl[r][g.runs[r]++] = mkrun(r, rstart, rfg, rbg, rul, run);
        }
    }

    if (g.scroll > 0) {                              /* in history: no live cursor */
        lv_obj_add_flag(g.cursor, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g.cursor, LV_OBJ_FLAG_HIDDEN);
        VTermPos cur;
        vterm_state_get_cursorpos(g.state, &cur);
        lv_obj_set_pos(g.cursor, cur.col * g.cell_w, cur.row * g.cell_h);
        lv_obj_move_foreground(g.cursor);
    }

    pthread_mutex_unlock(&g.mtx);
}

static void render_cb(lv_timer_t *t)
{
    (void)t;
    if (g.paused) return;         /* an overlay covers the terminal — don't repaint/raise cursor */
    if (!g.dirty) return;
    do_render();
}

/* Force one repaint even while paused — used by copy mode to reflect a
 * history scroll without letting live output repaint over the selection. */
void term_render_once(void) { if (g.vt) do_render(); }

#if defined(SSH_TERM_TEST_HOOKS)
/* headless self-test: type g.test through the real key path (emulator/CI only) */
static void test_cb(lv_timer_t *t)
{
    for (const char *p = g.test; p && *p; p++) {   /* raw bytes -> shell (UTF-8 safe) */
        char c = (*p == '\n') ? '\r' : *p;
        pty_write(&g.pty, &c, 1);
    }
    lv_timer_delete(t);
    g.test_timer = NULL;
}
#endif

/* create the cursor on g.parent (row run-labels are built per-frame in render) */
static void build_grid(void)
{
    for (int r = 0; r < g.rows; r++) g.runs[r] = 0;
    g.cursor = lv_obj_create(g.parent);
    lv_obj_remove_flag(g.cursor, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(g.cursor, 0, 0);
    lv_obj_set_style_radius(g.cursor, 0, 0);
    lv_obj_set_style_pad_all(g.cursor, 0, 0);
    lv_obj_set_style_bg_color(g.cursor, lv_color_hex(COL_FG), 0);
    lv_obj_set_style_bg_opa(g.cursor, LV_OPA_50, 0);
    lv_obj_set_size(g.cursor, g.cell_w, g.cell_h);
    lv_obj_set_pos(g.cursor, 0, 0);
}

void term_resize(const lv_font_t *font, int cols, int rows, int cell_w, int cell_h)
{
    if (!g.vt) return;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (cols > MAX_COLS) cols = MAX_COLS;
    pthread_mutex_lock(&g.mtx);
    for (int r = 0; r < g.rows; r++) clear_runs(r);
    if (g.cursor) { lv_obj_delete(g.cursor); g.cursor = NULL; }
    g.font = font; g.cols = cols; g.rows = rows; g.cell_w = cell_w; g.cell_h = cell_h;
    g.scroll = 0;
    build_grid();
    vterm_set_size(g.vt, rows, cols);
    pty_resize(&g.pty, cols, rows);
    g.dirty = 1;
    pthread_mutex_unlock(&g.mtx);
}

void term_create(lv_obj_t *parent, const char *const argv[],
                 const lv_font_t *font, int cols, int rows,
                 int cell_w, int cell_h)
{
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (cols > MAX_COLS) cols = MAX_COLS;
    memset(&g, 0, sizeof(g));
    g.parent = parent;
    g.cols = cols; g.rows = rows; g.cell_w = cell_w; g.cell_h = cell_h; g.font = font;
    g.alive = 1;
    pthread_mutex_init(&g.mtx, NULL);
    g.inited = 1;

    build_grid();

    g.vt = vterm_new(rows, cols);
    vterm_set_utf8(g.vt, 1);
    vterm_output_set_callback(g.vt, out_cb, NULL);
    g.state = vterm_obtain_state(g.vt);
    g.vts = vterm_obtain_screen(g.vt);
    g.sb = calloc(SB_CAP, sizeof(sbline_t));          /* scrollback ring */
    vterm_screen_set_callbacks(g.vts, &SCB, NULL);    /* capture scrolled-off lines */
    vterm_screen_enable_altscreen(g.vts, 1);
    vterm_screen_reset(g.vts, 1);

    /* default colors: green on black (matches the native console) */
    VTermColor dfg, dbg;
    vterm_color_rgb(&dfg, 0x4C, 0xD9, 0x6A);
    vterm_color_rgb(&dbg, 0x00, 0x00, 0x00);
    vterm_state_set_default_colors(g.state, &dfg, &dbg);

    if (pty_open(&g.pty, argv, cols, rows) != 0) {
        g.runlbl[0][0] = mkrun(0, 0, 0xFF6B6B, 0x000000, 0, "pty open failed");
        g.runs[0] = 1;
        return;
    }

    g.running = 1;
    pthread_create(&g.reader, NULL, reader_fn, NULL);
    g.timer = lv_timer_create(render_cb, 40, NULL);
    g.dirty = 1;

    /* test hook: TERM_TEST="echo hi\n" feeds chars via term_feed_key() once the
     * shell is up, to verify the key->PTY->shell->render path headlessly. */
#if defined(SSH_TERM_TEST_HOOKS)
    g.test = getenv("TERM_TEST");
    if (g.test) g.test_timer = lv_timer_create(test_cb, 700, NULL);
#endif
}

void term_feed_key(uint32_t key)
{
    if (g.pty.fd < 0) return;
    char b[8]; int n = 0;
    /* Emulator tags Ctrl-<key> with 0x40000000 so it doesn't collide with the
     * LVGL nav keys below (Ctrl-C 0x03 vs LV_KEY_END, etc.). On the device the
     * key_item modifier path will deliver the same control bytes. */
    if (key & 0x40000000u) { b[0] = (char)(key & 0x1f); pty_write(&g.pty, b, 1); return; }
    /* Function/nav keys tagged 0x10000000|code by the host keyboard driver
     * (1-12=F1-F12, 13=Insert, 14=PageUp, 15=PageDown) -> xterm escapes. */
    if (key & 0x10000000u) {
        static const char *const FK[15] = {
            "\x1bOP",   "\x1bOQ",   "\x1bOR",   "\x1bOS",     /* F1-F4  */
            "\x1b[15~", "\x1b[17~", "\x1b[18~", "\x1b[19~",   /* F5-F8  */
            "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~",   /* F9-F12 */
            "\x1b[2~",  "\x1b[5~",  "\x1b[6~",                /* Ins PgUp PgDn */
        };
        int i = (int)(key & 0xFF) - 1;
        if (i >= 0 && i < 15) pty_write(&g.pty, FK[i], strlen(FK[i]));
        return;
    }
    switch (key) {
    case LV_KEY_ENTER:     b[0] = '\r'; n = 1; break;
    case LV_KEY_BACKSPACE: b[0] = 0x7f; n = 1; break;
    case LV_KEY_ESC:       b[0] = 0x1b; n = 1; break;
    case LV_KEY_NEXT:      b[0] = '\t'; n = 1; break;   /* Tab */
    case LV_KEY_PREV:      n = sprintf(b, "\x1b[5~"); break;   /* PageUp from hosts that
                              deliver LV_KEY_PREV (was falling through as Ctrl-K) */
    case LV_KEY_UP:        n = sprintf(b, "\x1b[A"); break;
    case LV_KEY_DOWN:      n = sprintf(b, "\x1b[B"); break;
    case LV_KEY_RIGHT:     n = sprintf(b, "\x1b[C"); break;
    case LV_KEY_LEFT:      n = sprintf(b, "\x1b[D"); break;
    case LV_KEY_HOME:      n = sprintf(b, "\x1b[H"); break;
    case LV_KEY_END:       n = sprintf(b, "\x1b[F"); break;
    case LV_KEY_DEL:       n = sprintf(b, "\x1b[3~"); break;
    default:
        if (key >= 0x20) n = cp_to_utf8(key, b);            /* printable / unicode */
        else if (key >= 1 && key < 0x20) { b[0] = (char)key; n = 1; }  /* Ctrl-A etc.
            (those not shadowed by LV_KEY_*; full Ctrl needs the key_item path) */
        break;
    }
    if (n > 0) pty_write(&g.pty, b, (size_t)n);
}

void term_send_bytes(const char *buf, int n)
{
    if (g.pty.fd >= 0 && n > 0) pty_write(&g.pty, buf, (size_t)n);
}

int term_is_alive(void)
{
    return g.alive && g.pty.fd >= 0;
}

unsigned term_rx_seq(void)   /* increments while output arrives (lock-free reader) */
{
    return atomic_load_explicit(&g.rx_seq, memory_order_relaxed);
}

void term_destroy(void)
{
    if (!g.inited) return;        /* idempotent: safe to call with no live session */
    g.inited = 0;
    g.running = 0;
    /* Order matters: hang up the child first so the reader's blocking read()
     * returns EOF, JOIN the reader, THEN close the fd. Closing the master while
     * the reader is parked in read() on it deadlocks on macOS. */
    pty_terminate(&g.pty);
    if (g.reader) { pthread_join(g.reader, NULL); g.reader = 0; }
    pty_close(&g.pty);
    if (g.timer) { lv_timer_delete(g.timer); g.timer = NULL; }
    if (g.vt) { vterm_free(g.vt); g.vt = NULL; }
    if (g.sb) { free(g.sb); g.sb = NULL; }
    g.sb_n = g.sb_head = g.scroll = 0;
    pthread_mutex_destroy(&g.mtx);
}
