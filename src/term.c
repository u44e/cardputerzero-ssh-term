/* term.c — Phase 1 terminal core: PTY + libvterm + per-row LVGL labels.
 * Monochrome (green-on-black) for now; SGR colors come in a later phase. */
#include "term.h"
#include "pty.h"
#include "logsink.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vterm.h>

#define MAX_ROWS 40
#define MAX_COLS 128
#define MAX_RUNS 28          /* colored runs per row */
#define COL_FG   0x4CD96A    /* default fg (green, matches native console) */

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
    int           inited;      /* resources live (mutex/vt/reader) — destroy guard */
    int           paused;      /* freeze rendering while an overlay covers the term */
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

static void clear_runs(int r)
{
    for (int i = 0; i < g.runs[r]; i++)
        if (g.runlbl[r][i]) { lv_obj_delete(g.runlbl[r][i]); g.runlbl[r][i] = NULL; }
    g.runs[r] = 0;
}

static lv_obj_t *mkrun(int r, int col, uint32_t rgb, const char *txt)
{
    lv_obj_t *l = lv_label_create(g.parent);
    lv_obj_set_style_text_font(l, g.font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(rgb), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_text_letter_space(l, 0, 0);
    lv_obj_set_style_text_line_space(l, 0, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(l, col * g.cell_w, r * g.cell_h);
    lv_label_set_text(l, txt);
    return l;
}

/* render: cells -> per color-run labels (LVGL thread) */
void term_render_pause(int paused) { g.paused = paused ? 1 : 0; }

static void render_cb(lv_timer_t *t)
{
    (void)t;
    if (g.paused) return;         /* an overlay covers the terminal — don't repaint/raise cursor */
    if (!g.dirty) return;
    pthread_mutex_lock(&g.mtx);
    g.dirty = 0;

    char run[MAX_COLS * 4 + 1];
    for (int r = 0; r < g.rows; r++) {
        clear_runs(r);
        int started = 0, rstart = 0, li = 0;
        uint32_t rcol = 0;
        for (int c = 0; c < g.cols; ) {
            VTermPos pos; pos.row = r; pos.col = c;
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(g.vts, pos, &cell)) { c++; continue; }
            uint32_t cp = cell.chars[0]; if (cp == 0 || cp == (uint32_t)-1) cp = ' ';
            uint32_t rgb = cell_rgb(&cell);
            if (!started) { started = 1; rstart = c; rcol = rgb; li = 0; }
            else if (rgb != rcol) {
                run[li] = 0;
                if (g.runs[r] < MAX_RUNS) g.runlbl[r][g.runs[r]++] = mkrun(r, rstart, rcol, run);
                rstart = c; rcol = rgb; li = 0;
            }
            if (li < MAX_COLS * 4 - 4) li += cp_to_utf8(cp, run + li);
            c += (cell.width > 0 ? cell.width : 1);
        }
        if (started) {
            run[li] = 0;
            if (g.runs[r] < MAX_RUNS) g.runlbl[r][g.runs[r]++] = mkrun(r, rstart, rcol, run);
        }
    }

    VTermPos cur;
    vterm_state_get_cursorpos(g.state, &cur);
    lv_obj_set_pos(g.cursor, cur.col * g.cell_w, cur.row * g.cell_h);
    lv_obj_move_foreground(g.cursor);

    pthread_mutex_unlock(&g.mtx);
}

#if defined(SSH_TERM_TEST_HOOKS)
/* headless self-test: type g.test through the real key path (emulator/CI only) */
static void test_cb(lv_timer_t *t)
{
    for (const char *p = g.test; p && *p; p++) {
        if (*p == '\n') term_feed_key(LV_KEY_ENTER);
        else            term_feed_key((uint32_t)(unsigned char)*p);
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
    vterm_screen_enable_altscreen(g.vts, 1);
    vterm_screen_reset(g.vts, 1);

    /* default colors: green on black (matches the native console) */
    VTermColor dfg, dbg;
    vterm_color_rgb(&dfg, 0x4C, 0xD9, 0x6A);
    vterm_color_rgb(&dbg, 0x00, 0x00, 0x00);
    vterm_state_set_default_colors(g.state, &dfg, &dbg);

    if (pty_open(&g.pty, argv, cols, rows) != 0) {
        g.runlbl[0][0] = mkrun(0, 0, 0xFF6B6B, "pty open failed");
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
    switch (key) {
    case LV_KEY_ENTER:     b[0] = '\r'; n = 1; break;
    case LV_KEY_BACKSPACE: b[0] = 0x7f; n = 1; break;
    case LV_KEY_ESC:       b[0] = 0x1b; n = 1; break;
    case LV_KEY_NEXT:      b[0] = '\t'; n = 1; break;   /* Tab */
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
    pthread_mutex_destroy(&g.mtx);
}
