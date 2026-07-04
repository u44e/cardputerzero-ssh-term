/*
 * ssh_term — connection profiles, editor, terminal.
 *   SCR_PROFILES  list (Up/Dn, Enter connect, e edit, n new, d del, l logs)
 *   SCR_EDITOR    edit a profile (Up/Dn field, Enter edit/toggle, s save, ESC back)
 *   SCR_TERM      live terminal (all keys -> PTY) + status bar
 */
#include <cz_app.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include "config.h"
#include "term.h"
#include "logsink.h"
#include "sendfile.h"
#include "vpn.h"

#if defined(APP_EMU)
#define MONO_PATH "/System/Library/Fonts/Menlo.ttc"
#define UI_JP_PATH "/System/Library/Fonts/ヒラギノ角ゴシック W4.ttc"
#else
#define MONO_PATH "/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf"
#define UI_JP_PATH "/usr/share/APPLaunch/share/font/AlibabaPuHuiTi-3-55-Regular.ttf"
#endif

#define COL_BG     0x1A1A2E
#define COL_TITLE  0x3AD8FF
#define COL_TEXT   0xECECF2
#define COL_DIM    0xA2A2C0
#define COL_HILITE 0x2C2C52
#define COL_CYAN   0x3AD8FF
#define COL_AMBER  0xFFB82E
#define COL_GREEN  0x4CD96A
#define COL_RED    0xFF6B6B

enum { SCR_PROFILES, SCR_EDITOR, SCR_TERM, SCR_LOGS, SCR_LOGVIEW, SCR_MENU, SCR_FILES,
       SCR_SEND, SCR_DIALOG, SCR_TERM_DISC,     /* _DISC = session ended, output frozen for review */
       SCR_MACROS, SCR_MACRO_EDIT };            /* quick-send macro picker + editor (overlays) */

static lv_obj_t   *g_root;
static int         g_scr = SCR_PROFILES;
static int         g_sel = 0;
static int         g_cur_pidx = 0;         /* profile of the live session */
static int         g_lang = 0;             /* 0 = en, 1 = ja */
static lv_group_t *g_grp;
static lv_obj_t   *g_cap;
static lv_timer_t *g_watch;

static const lv_font_t *g_mono;
static int g_cols = 45, g_rows = 12, g_cw = 7, g_ch = 12;
static lv_obj_t   *g_overlay;              /* menu/dialog/send/files panel (child of backdrop) */
static lv_obj_t   *g_backdrop;             /* full-screen opaque cover behind the panel */
static lv_obj_t   *g_scrollhint;           /* hint shown while in terminal scroll mode */
static lv_obj_t   *g_copybar, *g_copyhint; /* line-copy mode highlight + hint */
static int         g_copy_row = -1;        /* -1 = copy mode off; else highlighted row */
static lv_obj_t   *g_sendprog;             /* file-injection progress badge (status bar) */
static lv_obj_t   *g_logview_ta;           /* log viewer scrollable textarea */
static lv_obj_t   *g_logprompt;            /* log viewer bottom line (guide / find box) */
static char        g_logbuf[8192];         /* stripped log text (searched in place) */
static char        g_logfind[48];          /* last search query */
static int         g_logfinding;           /* 1 = typing a query */
static int         g_logfind_pos;          /* byte offset to resume the next search from */

/* editor state */
static int  g_edit_idx = 0;
static int  g_field = 0;
static int  g_editing = 0;
static char g_ebuf[128];

/* logs browser state */
static int  g_log_sel = 0;

/* top-right status bar (clock / wifi / battery) */
static lv_obj_t *g_sb_time, *g_sb_batt, *g_sb_wifi[4];
static int       s_batt = -1, s_wifi = -1;   /* % and 0-4 bars, -1 unknown */

static void show_profiles(void);
static void show_editor(int idx);
static void show_logs(void);
static void connect_profile(int i);
static void open_send(void);
static void open_files(void);
static void open_dialog(int prev, uint32_t accent, const char *title,
                        const char *msg, const char *yeslbl, void (*onyes)(void));

/* ---------------- font (size-selectable) ---------------- */
static const int SIZES[3] = { 12, 16, 20 };
static const lv_font_t *g_fontcache[3];
static int g_size_idx = 0;

static int size_to_idx(const char *s)
{
    int px = (s && s[0]) ? atoi(s) : 12;
    for (int i = 0; i < 3; i++) if (SIZES[i] == px) return i;
    return 0;
}

/* load font for size index; fill globals g_mono/g_cols/g_rows/g_cw/g_ch */
static void load_font_idx(int idx)
{
    if (idx < 0 || idx > 2) idx = 0;
    g_size_idx = idx;
    if (!g_fontcache[idx]) {
        lv_freetype_init(256);
        lv_font_t *f = lv_freetype_font_create(MONO_PATH,
            LV_FREETYPE_FONT_RENDER_MODE_BITMAP, SIZES[idx], LV_FREETYPE_FONT_STYLE_NORMAL);
        if (f) {   /* CJK fallback so Japanese/CJK output isn't tofu in the terminal */
            f->fallback = lv_freetype_font_create(UI_JP_PATH,
                LV_FREETYPE_FONT_RENDER_MODE_BITMAP, SIZES[idx], LV_FREETYPE_FONT_STYLE_NORMAL);
        }
        g_fontcache[idx] = f;
    }
    const lv_font_t *f = g_fontcache[idx];
    if (f) {
        int px = SIZES[idx];
        int w = lv_font_get_glyph_width((lv_font_t *)f, 'M', 0);
        if (w < 4) w = px * 7 / 12;
        g_mono = f; g_ch = px; g_cw = w;
        g_cols = 320 / w; g_rows = 154 / px;   /* grid above the status bar */
    } else {
        g_mono = &lv_font_unscii_8; g_cw = 8; g_ch = 8; g_cols = 40; g_rows = 18;
    }
}

/* ---------------- i18n ---------------- */
static const char *tr(const char *en, const char *ja) { return g_lang ? ja : en; }

/* UI font: Montserrat for en; a CJK font (freetype) for ja so Japanese renders.
 * Falls back to Montserrat (tofu) if the CJK font is unavailable. */
static const lv_font_t *ui_font(int px)
{
    static const lv_font_t *jp14, *jp12;
    if (g_lang) {
        const lv_font_t **slot = (px >= 14) ? &jp14 : &jp12;
        if (!*slot) {
            lv_freetype_init(256);
            lv_font_t *fp = lv_freetype_font_create(UI_JP_PATH, LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                                    px >= 14 ? 14 : 12, LV_FREETYPE_FONT_STYLE_NORMAL);
            if (fp) {
                /* CJK freetype metrics sit lower than Montserrat; match the baseline
                 * and line height so JA UI text lines up with the EN layout. */
                const lv_font_t *ms = (px >= 14) ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
                fp->base_line   = ms->base_line;
                fp->line_height = ms->line_height;
            }
            *slot = fp;
        }
        if (*slot) return *slot;
    }
    return px >= 14 ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
}

/* ---------------- helpers ---------------- */
static lv_obj_t *mklabel_on(lv_obj_t *parent, const lv_font_t *f, uint32_t color, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *mklabel(const lv_font_t *f, uint32_t color, int x, int y, const char *txt)
{
    return mklabel_on(g_root, f, color, x, y, txt);
}

static lv_obj_t *mkrect(uint32_t color, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(g_root);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    return o;
}

static void attach_capture(void)
{
    if (g_grp) { lv_group_delete(g_grp); g_grp = NULL; }
    g_cap = lv_obj_create(g_root);
    lv_obj_set_size(g_cap, 1, 1);
    lv_obj_set_pos(g_cap, -10, -10);
    lv_obj_clear_flag(g_cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    extern void key_cb(lv_event_t *e);
    lv_obj_add_event_cb(g_cap, key_cb, LV_EVENT_KEY, NULL);
    g_grp = lv_group_create();
    lv_indev_t *id = lv_indev_get_next(NULL);
    while (id) {
        if (lv_indev_get_type(id) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(id, g_grp);
        id = lv_indev_get_next(id);
    }
    lv_group_add_obj(g_grp, g_cap);
    lv_group_focus_obj(g_cap);
}

/* ---------------- top status bar (clock / wifi / battery) ---------------- */
static int read_battery(void)
{
#if defined(APP_EMU)
    FILE *fp = popen("pmset -g batt 2>/dev/null", "r");      /* macOS */
    if (!fp) return -1;
    char line[256]; int pct = -1;
    while (fgets(line, sizeof line, fp)) {
        char *pc = strchr(line, '%');
        if (pc) { char *s = pc; while (s > line && isdigit((unsigned char)s[-1])) s--; pct = atoi(s); break; }
    }
    pclose(fp);
    return pct;
#else
    const char *paths[] = { "/sys/class/power_supply/BAT0/capacity",
                            "/sys/class/power_supply/BAT1/capacity",
                            "/sys/class/power_supply/battery/capacity" };
    for (int i = 0; i < 3; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) { int v = -1, ok = fscanf(f, "%d", &v); fclose(f); if (ok == 1) return v; }
    }
    return -1;
#endif
}

static int read_wifi(void)   /* 0-4 bars, -1 unknown */
{
#if defined(APP_EMU)
    return 3;   /* emulator has no easy Wi-Fi CLI; device reads the real value */
#else
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return -1;
    char line[256]; int bars = -1;
    if (fgets(line, sizeof line, f) && fgets(line, sizeof line, f) && fgets(line, sizeof line, f)) {
        char iface[40]; int st; float link = -1;
        if (sscanf(line, " %39[^:]: %d %f", iface, &st, &link) >= 3 && link >= 0) {
            bars = (int)(link * 4 / 70);   /* link quality 0-70 -> 0-4 */
            if (bars > 4) bars = 4; if (bars < 0) bars = 0;
        }
    }
    fclose(f);
    return bars;
#endif
}

static void statusbar_set(void)
{
    if (!g_sb_time) return;
    time_t t = time(NULL); struct tm m; localtime_r(&t, &m);
    char tb[8]; snprintf(tb, sizeof tb, "%02d:%02d", m.tm_hour, m.tm_min);
    lv_label_set_text(g_sb_time, tb);

    static time_t last = 0;     /* throttle the slow battery/wifi reads */
    if (last == 0 || t - last >= 20) { last = t; s_batt = read_battery(); s_wifi = read_wifi(); }

    char bb[16];
    if (s_batt >= 0) snprintf(bb, sizeof bb, "%d%%", s_batt);
    else             snprintf(bb, sizeof bb, "--%%");
    lv_label_set_text(g_sb_batt, bb);
    lv_obj_set_style_text_color(g_sb_batt, lv_color_hex(s_batt >= 0 && s_batt < 15 ? COL_RED : COL_TEXT), 0);

    /* lay the cluster out flush to the right edge (battery width varies) */
    int rx = 313;
    int bx = rx - (int)strlen(bb) * 8;        /* battery % */
    lv_obj_set_pos(g_sb_batt, bx, 4);
    int wx = bx - 8 - 18;                      /* wifi bars (18px wide) */
    for (int i = 0; i < 4; i++) {
        int h = 4 + i * 2;
        lv_obj_set_pos(g_sb_wifi[i], wx + i * 5, 16 - h);
        lv_obj_set_style_bg_color(g_sb_wifi[i],
            lv_color_hex(i < (s_wifi < 0 ? 0 : s_wifi) ? COL_CYAN : COL_HILITE), 0);
    }
    lv_obj_set_pos(g_sb_time, wx - 8 - 30, 4); /* clock (HH:MM) */
}

static void draw_statusbar(void)   /* top-right: HH:MM  wifi-bars  battery% */
{
    g_sb_time = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 4, "");
    for (int i = 0; i < 4; i++) { int h = 4 + i * 2; g_sb_wifi[i] = mkrect(COL_HILITE, 0, 16 - h, 3, h); }
    g_sb_batt = mklabel(&lv_font_montserrat_12, COL_TEXT, 0, 4, "");
    statusbar_set();
}

static void statusbar_tick(lv_timer_t *t) { (void)t; if (g_scr == SCR_PROFILES) statusbar_set(); }

/* ---------------- profiles ---------------- */
static void show_profiles(void)
{
    g_scr = SCR_PROFILES;
    lv_obj_clean(g_root);
    g_sb_time = NULL; g_scrollhint = NULL;   /* labels were just deleted */
    g_copybar = NULL; g_copyhint = NULL; g_copy_row = -1; g_sendprog = NULL;
    g_logview_ta = NULL; g_logprompt = NULL;
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    mklabel(ui_font(14), COL_TITLE, 8, 5, tr("Sessions","セッション"));
    draw_statusbar();                 /* clock / wifi / battery (top-right) */

    int top = 28, rh = 22, n = config_count();
    if (g_sel >= n) g_sel = n > 0 ? n - 1 : 0;
    for (int i = 0; i < n; i++) {
        const profile_t *p = config_get(i);
        int y = top + i * rh;
        if (i == g_sel) {
            mkrect(COL_HILITE, 0, y - 1, 320, rh - 2);
            mkrect(COL_CYAN, 0, y - 1, 3, rh - 2);
        }
        mklabel(ui_font(14), COL_TEXT, 12, y + 2, p->name);
        char meta[180];
        if      (!strcmp(p->proto, "shell"))  snprintf(meta, sizeof(meta), "shell");
        else if (!strcmp(p->proto, "serial")) snprintf(meta, sizeof(meta), "serial  %s:%s",
                                                       p->host, p->port);   /* device:baud, no user */
        else snprintf(meta, sizeof(meta), "%s  %s%s%s:%s", p->proto,
                      p->user, p->user[0] ? "@" : "", p->host, p->port);
        lv_obj_t *m = mklabel(ui_font(12), COL_DIM, 150, y + 3, meta);
        if (!strcmp(p->proto, "telnet")) lv_obj_set_style_text_color(m, lv_color_hex(COL_AMBER), 0);
        if (p->log) { lv_obj_t *L = mklabel(ui_font(12), COL_AMBER, 0, 0, "L");
                      lv_obj_align(L, LV_ALIGN_TOP_RIGHT, -8, y + 3); }
        if (strcmp(p->proto, "serial") && p->vpn_type[0] && strcmp(p->vpn_type, "none")) {
            lv_obj_t *V = mklabel(ui_font(12), COL_GREEN, 0, 0, "V");
            lv_obj_align(V, LV_ALIGN_TOP_RIGHT, p->log ? -24 : -8, y + 3); }
    }

    lv_obj_t *guide = mklabel(ui_font(12), COL_DIM, 0, 0,
                              tr(LV_SYMBOL_UP LV_SYMBOL_DOWN " Enter e:edit n:new c:copy d:del l:logs g:JP",
                                 "↑↓ Enter:接続 e:編集 n:新規 c:複製 d:削除 l:ログ g:EN"));
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

/* ---------------- editor (fields vary by VPN type, iPhone-style) ---------------- */
typedef struct {
    const char *en, *ja;
    int    kind;    /* 0=text 1=proto 2=vpntype 3=log 4=size 5=serial-format */
    char  *buf;     /* text storage (kind 0) */
    size_t sz;
    int    secret;  /* mask the value (password/secret) */
} efield_t;
static efield_t g_ef[16];
static int      g_efn = 0;

static void build_fields(profile_t *p)
{
    int n = 0;
#define EF(EN,JA,K,B,S,SEC) do{ g_ef[n].en=(EN); g_ef[n].ja=(JA); g_ef[n].kind=(K); \
        g_ef[n].buf=(B); g_ef[n].sz=(S); g_ef[n].secret=(SEC); n++; }while(0)
    int serial = !strcmp(p->proto, "serial");   /* USB-serial: host=device, port=baud */
    EF("Name","名前",0,p->name,sizeof p->name,0);
    EF(serial ? "Device" : "Host", serial ? "デバイス" : "ホスト",0,p->host,sizeof p->host,0);
    EF(serial ? "Baud"   : "Port", serial ? "ボーレート" : "ポート",0,p->port,sizeof p->port,0);
    if (serial) EF("Format","形式",5,0,0,0);            /* 8N1 / 7E1 / 7O1 / 8N2 */
    if (!serial) EF("User","ユーザ",0,p->user,sizeof p->user,0);
    if (!strcmp(p->proto, "ssh"))
        EF("Key","鍵ファイル",0,p->key,sizeof p->key,0); /* optional identity (-i) */
    EF("Proto","接続種別",1,0,0,0);
    if (!serial) {   /* VPN is meaningless for a local serial console */
        EF("VPN type","VPN方式",2,0,0,0);
        /* OS holds the VPN config + secrets; the profile only names the connection to
         * bring up (nmcli / wg-quick / ipsec by name). Tailscale needs no name. */
        if (p->vpn_type[0] && strcmp(p->vpn_type, "none") && strcmp(p->vpn_type, "tailscale"))
            EF("Connection","接続名",0,p->vpn,sizeof p->vpn,0);
    }
    EF("Log","ログ",3,0,0,0);
    EF("Size","文字サイズ",4,0,0,0);
#undef EF
    g_efn = n;
}

static const char *field_label(int f) { return tr(g_ef[f].en, g_ef[f].ja); }
static int field_is_text(int f) { return g_ef[f].kind == 0; }
static void field_set_text(int f, const char *v)
{
    if (g_ef[f].buf) snprintf(g_ef[f].buf, g_ef[f].sz, "%s", v);
}
static void field_value(profile_t *p, int f, char *out, size_t n)
{
    efield_t *e = &g_ef[f];
    switch (e->kind) {
    case 1: snprintf(out, n, "< %s >", p->proto); break;
    case 2: snprintf(out, n, "< %s >", p->vpn_type[0] ? p->vpn_type : "none"); break;
    case 3: snprintf(out, n, tr("[%s] save session log","[%s] セッションログ保存"), p->log ? "x" : " "); break;
    case 4: snprintf(out, n, "< %spx >", p->size[0] ? p->size : "12"); break;
    case 5: snprintf(out, n, "< %s >", p->sfmt[0] ? p->sfmt : "8N1"); break;
    default:
        if (e->secret && e->buf && e->buf[0]) snprintf(out, n, "********");
        else snprintf(out, n, "%s", (e->buf && e->buf[0]) ? e->buf : tr("(none)","(なし)"));
        break;
    }
}

static void show_editor(int idx)
{
    g_scr = SCR_EDITOR;
    g_edit_idx = idx;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    profile_t *p = config_mutable(idx);
    if (!p) { show_profiles(); return; }
    build_fields(p);
    if (g_field >= g_efn) g_field = g_efn - 1;

    char title[64]; snprintf(title, sizeof(title), tr("Edit: %s","編集: %s"), p->name);
    mklabel(ui_font(14), COL_TITLE, 8, 4, title);

    int top = 24, rh = 14;
    const int VIS = 9;
    int start = g_field >= VIS ? g_field - VIS + 1 : 0;
    for (int row = 0; row < VIS && start + row < g_efn; row++) {
        int f = start + row, y = top + row * rh;
        if (f == g_field) { mkrect(COL_HILITE, 0, y, 320, rh - 1); mkrect(COL_CYAN, 0, y, 3, rh - 1); }
        mklabel(ui_font(12), f == g_field ? COL_TEXT : COL_DIM, 12, y + 1, field_label(f));
        char val[160];
        if (g_editing && f == g_field && field_is_text(f)) snprintf(val, sizeof(val), "%s_", g_ebuf);
        else                                               field_value(p, f, val, sizeof(val));
        mklabel(ui_font(12), g_ef[f].kind ? COL_CYAN : COL_TEXT, 92, y + 1, val);
    }
    if (start > 0) {                 /* more above: ▲ at top-right */
        lv_obj_t *a = mklabel(ui_font(12), COL_CYAN, 0, 0, LV_SYMBOL_UP);
        lv_obj_align(a, LV_ALIGN_TOP_RIGHT, -4, top);
    }
    if (start + VIS < g_efn) {       /* more below: ▼ at bottom-right of the list */
        lv_obj_t *a = mklabel(ui_font(12), COL_CYAN, 0, 0, LV_SYMBOL_DOWN);
        lv_obj_align(a, LV_ALIGN_TOP_RIGHT, -4, top + (VIS - 1) * rh);
    }

    lv_obj_t *guide = mklabel(ui_font(12), COL_DIM, 0, 0,
        g_editing ? tr("type...  Enter:ok  ESC:cancel","入力...  Enter:確定  ESC:取消")
                  : tr(LV_SYMBOL_UP LV_SYMBOL_DOWN " Enter:edit  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT ":toggle  s:save  ESC:back",
                       "↑↓ Enter:編集 ←→:切替 s:保存 ESC:戻る"));
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

static void editor_toggle(profile_t *p, int dir)
{
    switch (g_ef[g_field].kind) {
    case 1: {
        const char *seq[] = { "ssh", "telnet", "shell", "serial" }; int m = 4, cur = 0;
        for (int i = 0; i < m; i++) if (!strcmp(p->proto, seq[i])) cur = i;
        snprintf(p->proto, sizeof(p->proto), "%s", seq[(cur + (dir >= 0 ? 1 : m - 1)) % m]);
        break; }
    case 2: {
        int n = vpn_type_count(), cur = 0;
        for (int i = 0; i < n; i++) if (!strcmp(p->vpn_type, VPN_TYPES[i])) cur = i;
        snprintf(p->vpn_type, sizeof(p->vpn_type), "%s", VPN_TYPES[(cur + (dir >= 0 ? 1 : n - 1)) % n]);
        break; }
    case 3: p->log = !p->log; break;
    case 4: {
        int idx = (size_to_idx(p->size) + (dir >= 0 ? 1 : 2)) % 3;
        snprintf(p->size, sizeof(p->size), "%d", SIZES[idx]); break; }
    case 5: {   /* serial format presets (network-gear console defaults) */
        static const char *const SFMTS[] = { "8N1", "7E1", "7O1", "8N2" };
        int m = 4, cur = 0;
        for (int i = 0; i < m; i++) if (!strcmp(p->sfmt, SFMTS[i])) cur = i;
        snprintf(p->sfmt, sizeof(p->sfmt), "%s", SFMTS[(cur + (dir >= 0 ? 1 : m - 1)) % m]);
        break; }
    }
}

/* toggle, then keep the cursor on the same logical row: crossing the serial
 * boundary reflows the field list (User/VPN rows appear/disappear), so a raw
 * numeric g_field would land on a different field and mis-toggle it. */
static void editor_retoggle(profile_t *p, int dir)
{
    int kind = g_ef[g_field].kind;
    editor_toggle(p, dir);
    if (kind == 1) {                       /* proto changed -> re-anchor on the Proto row */
        build_fields(p);
        for (int i = 0; i < g_efn; i++) if (g_ef[i].kind == 1) { g_field = i; break; }
    }
    show_editor(g_edit_idx);
}

static void key_editor(uint32_t k)
{
    profile_t *p = config_mutable(g_edit_idx);
    if (!p) { show_profiles(); return; }
    build_fields(p);
    if (g_field >= g_efn) g_field = g_efn - 1;

    if (g_editing) {
        if (k == LV_KEY_ENTER)      { field_set_text(g_field, g_ebuf); g_editing = 0; show_editor(g_edit_idx); }
        else if (k == LV_KEY_ESC)   { g_editing = 0; show_editor(g_edit_idx); }
        else if (k == LV_KEY_BACKSPACE) { size_t l = strlen(g_ebuf); if (l) g_ebuf[l-1] = 0; show_editor(g_edit_idx); }
        else if (k >= 0x20 && k < 0x7f) { size_t l = strlen(g_ebuf); if (l < sizeof(g_ebuf)-1) { g_ebuf[l]=(char)k; g_ebuf[l+1]=0; } show_editor(g_edit_idx); }
        return;
    }
    switch (k) {
    case LV_KEY_UP:    if (g_field > 0) g_field--; show_editor(g_edit_idx); break;
    case LV_KEY_DOWN:  if (g_field < g_efn - 1) g_field++; show_editor(g_edit_idx); break;
    case LV_KEY_LEFT:  editor_retoggle(p, -1); break;
    case LV_KEY_RIGHT: editor_retoggle(p, +1); break;
    case LV_KEY_ENTER:
        if (field_is_text(g_field)) {
            snprintf(g_ebuf, sizeof(g_ebuf), "%s", g_ef[g_field].buf ? g_ef[g_field].buf : "");
            g_editing = 1; show_editor(g_edit_idx);
        } else editor_retoggle(p, +1);
        break;
    case LV_KEY_ESC: show_profiles(); break;
    default:
        if (k == 's') { config_save(); show_profiles(); }
        break;
    }
}

/* ---------------- profiles key ---------------- */
static void do_delete_cb(void) { config_delete(g_sel); config_save(); show_profiles(); }

static void key_profiles(uint32_t k)
{
    int n = config_count();
    switch (k) {
    case LV_KEY_UP:    if (g_sel > 0) { g_sel--; show_profiles(); } break;
    case LV_KEY_DOWN:  if (g_sel < n - 1) { g_sel++; show_profiles(); } break;
    case LV_KEY_ENTER: connect_profile(g_sel); break;
    default:
        if (k == 'e' && n > 0) { g_field = 0; g_editing = 0; show_editor(g_sel); }
        else if (k == 'n') { int i = config_add(); if (i >= 0) { g_sel = i; g_field = 0; g_editing = 0; show_editor(i); } }
        else if (k == 'c' && n > 0) {   /* duplicate: clone + open the editor on the copy */
            int i = config_dup(g_sel); if (i >= 0) { g_sel = i; g_field = 0; g_editing = 0; show_editor(i); }
        }
        else if (k == 'd' && n > 0) {
            char m[80]; snprintf(m, sizeof(m), tr("Delete \"%s\" ?","\"%s\" を削除?"), config_get(g_sel)->name);
            open_dialog(SCR_PROFILES, COL_RED, tr("Confirm","確認"), m, tr("Delete","削除"), do_delete_cb);
        }
        else if (k == 'l') { g_log_sel = 0; show_logs(); }
        else if (k == 'g') { g_lang = !g_lang; config_set_lang(g_lang); config_save(); show_profiles(); }
        break;
    }
}

/* ---------------- terminal ---------------- */
static lv_obj_t *g_status;
static int      g_session_up;   /* a session exists (live or under an overlay) */
static uint32_t g_disc_tick;    /* when SCR_TERM_DISC was entered (key grace period) */

static void add_status_bar(const profile_t *p, int logging)
{
    int by = 154;
    mkrect(0x0A0A10, 0, by, 320, 170 - by);
    mkrect(COL_DIM, 0, by, 320, 1);
    char s[96];
    snprintf(s, sizeof(s), tr("%s   CONNECTED%s   SIDE=menu","%s   接続中%s   SIDE=menu"),
             p->name, logging ? tr("   REC","   録画") : "");
    g_status = lv_label_create(g_root);
    lv_obj_set_style_text_font(g_status, ui_font(12), 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_pos(g_status, 6, by + 2);
    lv_label_set_text(g_status, s);
}

static void do_connect_now(void)   /* the actual connect (after any VPN gate) */
{
    int i = g_cur_pidx;
    const char *const *argv = config_argv(i);
    const profile_t *p = config_get(i);
    if (!argv || !p) { show_profiles(); return; }
    load_font_idx(size_to_idx(p->size));

    g_overlay = NULL; g_backdrop = NULL;   /* cleaned by lv_obj_clean below */
    g_scrollhint = NULL;                   /* ditto — reconnect path never passes show_profiles */
    g_copybar = NULL; g_copyhint = NULL; g_copy_row = -1; g_sendprog = NULL;
    term_render_pause(0);
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    if (p->log) logsink_open(p->name);
    term_create(g_root, argv, g_mono, g_cols, g_rows, g_cw, g_ch);
    add_status_bar(p, p->log);
    /* Japanese input is handled by the OS IME (fcitx5-mozc): composed text
     * arrives via SDL_TEXTINPUT and is forwarded to the PTY by term_feed_key. */
    attach_capture();
    g_session_up = 1;
    g_scr = SCR_TERM;
}

static void connect_profile(int i)
{
    const profile_t *p = config_get(i);
    if (!p) return;
    g_cur_pidx = i;
    /* serial is a local console — a stale vpn_type from a former ssh profile
     * must not gate it (the editor hides the VPN rows for serial) */
    if (strcmp(p->proto, "serial") && p->vpn_type[0] && strcmp(p->vpn_type, "none") && vpn_up(p) != 0) {
        char m[80]; snprintf(m, sizeof(m), tr("%s VPN did not come up","%s VPN を確立できませんでした"), p->vpn_type);
        open_dialog(SCR_PROFILES, COL_RED, tr("VPN failed","VPN失敗"), m, tr("Connect anyway","このまま接続"), do_connect_now);
        return;
    }
    do_connect_now();
}

/* ---- disconnected review + reconnect (session ended / connect failed) ---- */
static void end_session(void)   /* tear everything down and return to the list */
{
    sendfile_cancel();    /* stop an in-flight paced send before the PTY goes away */
    g_session_up = 0;
    term_destroy();
    logsink_close();
    vpn_down();
    show_profiles();
}

static void reconnect_session(void)   /* re-run the same profile through the VPN gate */
{
    sendfile_cancel();    /* a leftover send must not inject into the new session */
    g_session_up = 0;
    term_destroy();
    logsink_close();      /* do_connect_now reopens the log if the profile has log=1 */
    show_profiles();      /* clean base screen — the VPN-fail dialog may land on it */
    connect_profile(g_cur_pidx);   /* re-runs vpn_up (idempotent when the VPN is still up) */
}

/* Session died: keep the final output on screen (don't destroy the terminal) and
 * recolor the status bar so the user can read the last lines / reconnect. */
static void enter_disconnected(void)
{
    sendfile_cancel();    /* pointless against a dead PTY; must not outlive it */
    g_session_up = 0;
    logsink_close();      /* finalize the log; the rendered buffer stays visible */
    if (g_status) {
        lv_obj_set_style_text_color(g_status, lv_color_hex(COL_RED), 0);
        lv_label_set_text(g_status, tr("DISCONNECTED   Enter:close  r:reconnect",
                                       "切断   Enter:閉じる  r:再接続"));
    }
    g_disc_tick = lv_tick_get();   /* grace period: don't eat keystrokes in flight */
    g_scr = SCR_TERM_DISC;
}

/* ---------------- logs browser ---------------- */
static void show_logs(void)
{
    g_scr = SCR_LOGS;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    mklabel(ui_font(14), COL_TITLE, 8, 5, tr("Logs","ログ"));

    int n = logsink_list_count();
    char cnt[24]; snprintf(cnt, sizeof(cnt), tr("%d files","%d 件"), n);
    lv_obj_t *c = mklabel(ui_font(12), COL_DIM, 0, 0, cnt);
    lv_obj_align(c, LV_ALIGN_TOP_RIGHT, -8, 8);

    int top = 28, rh = 20;
    if (g_log_sel >= n) g_log_sel = n > 0 ? n - 1 : 0;
    for (int i = 0; i < n && i < 6; i++) {
        int y = top + i * rh;
        if (i == g_log_sel) { mkrect(COL_HILITE, 0, y - 1, 320, rh - 2); mkrect(COL_CYAN, 0, y - 1, 3, rh - 2); }
        mklabel(ui_font(12), i == g_log_sel ? COL_TEXT : COL_DIM, 12, y + 1, logsink_list_name(i));
    }
    if (n == 0) mklabel(ui_font(12), COL_DIM, 12, 32, tr("(no logs yet)","(ログなし)"));

    lv_obj_t *guide = mklabel(ui_font(12), COL_DIM, 0, 0, tr(LV_SYMBOL_UP LV_SYMBOL_DOWN " Enter:view  d:del  ESC:back","↑↓ Enter:表示 d:削除 ESC:戻る"));
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

static void logview_prompt(void)   /* bottom line: guide, or the find box while typing */
{
    if (!g_logprompt) return;
    char s[80];
    if (g_logfinding) snprintf(s, sizeof(s), tr("find: %s_","検索: %s_"), g_ebuf);
    else if (g_logfind[0]) snprintf(s, sizeof(s), tr("/:find  n:next  ESC:back   [%s]","/:検索 n:次 ESC:戻る [%s]"), g_logfind);
    else snprintf(s, sizeof(s), tr(LV_SYMBOL_UP LV_SYMBOL_DOWN ":scroll  /:find  ESC:back","↑↓:スクロール /:検索 ESC:戻る"));
    lv_label_set_text(g_logprompt, s);
}

/* jump the textarea cursor to the next match of g_logfind (wraps once). */
static void logview_search(void)
{
    if (!g_logfind[0] || !g_logview_ta) return;
    int len = (int)strlen(g_logbuf);
    if (g_logfind_pos > len) g_logfind_pos = 0;
    char *hit = strstr(g_logbuf + g_logfind_pos, g_logfind);
    if (!hit && g_logfind_pos > 0) hit = strstr(g_logbuf, g_logfind);   /* wrap to top */
    if (hit) {
        int off = (int)(hit - g_logbuf);
        int ci = 0;                                     /* byte offset -> codepoint index */
        for (int b = 0; b < off; b++) if (((unsigned char)g_logbuf[b] & 0xC0) != 0x80) ci++;
        lv_textarea_set_cursor_pos(g_logview_ta, ci);   /* scrolls the match into view */
        g_logfind_pos = off + 1;
    } else {
        if (g_logprompt) lv_label_set_text(g_logprompt, tr("not found","見つかりません"));
    }
}

static void show_logview(int i)
{
    g_scr = SCR_LOGVIEW;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    if (!g_mono) load_font_idx(0);

    mklabel(ui_font(12), COL_TITLE, 6, 4, logsink_list_name(i));
    lv_obj_t *ta = lv_textarea_create(g_root);
    lv_obj_set_style_text_font(ta, g_mono, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_size(ta, 320, 132);
    lv_obj_set_pos(ta, 0, 20);
    lv_textarea_set_cursor_click_pos(ta, false);

    logsink_read_stripped(i, g_logbuf, sizeof(g_logbuf));
    lv_textarea_set_text(ta, g_logbuf);
    lv_textarea_set_cursor_pos(ta, 0);
    g_logview_ta = ta;
    g_logfinding = 0; g_logfind_pos = 0; g_logfind[0] = 0;

    g_logprompt = mklabel(ui_font(12), COL_DIM, 0, 0, "");
    lv_obj_set_style_bg_color(g_logprompt, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_logprompt, LV_OPA_COVER, 0);
    lv_obj_set_width(g_logprompt, 320);
    lv_obj_align(g_logprompt, LV_ALIGN_BOTTOM_LEFT, 8, -2);
    logview_prompt();
    attach_capture();
}

static void key_logview(uint32_t k)
{
    if (g_logfinding) {
        if (k == LV_KEY_ENTER)      { snprintf(g_logfind, sizeof(g_logfind), "%s", g_ebuf); g_logfinding = 0; g_logfind_pos = 0; logview_search(); logview_prompt(); }
        else if (k == LV_KEY_ESC)   { g_logfinding = 0; logview_prompt(); }
        else if (k == LV_KEY_BACKSPACE) { size_t l = strlen(g_ebuf); if (l) g_ebuf[l-1] = 0; logview_prompt(); }
        else if (k >= 0x20 && k < 0x7f) { size_t l = strlen(g_ebuf); if (l < sizeof(g_ebuf)-1) { g_ebuf[l]=(char)k; g_ebuf[l+1]=0; } logview_prompt(); }
        return;
    }
    if (k == LV_KEY_ESC) show_logs();
    else if (k == LV_KEY_DOWN && g_logview_ta) lv_obj_scroll_by(g_logview_ta, 0, -28, LV_ANIM_OFF);
    else if (k == LV_KEY_UP && g_logview_ta)   lv_obj_scroll_by(g_logview_ta, 0,  28, LV_ANIM_OFF);
    else if (k == '/') { g_logfinding = 1; g_ebuf[0] = 0; logview_prompt(); }
    else if ((k == 'n' || k == 'N') && g_logfind[0]) { logview_search(); logview_prompt(); }
}

static void key_logs(uint32_t k)
{
    int n = logsink_list_count();
    switch (k) {
    case LV_KEY_UP:    if (g_log_sel > 0) { g_log_sel--; show_logs(); } break;
    case LV_KEY_DOWN:  if (g_log_sel < n - 1) { g_log_sel++; show_logs(); } break;
    case LV_KEY_ENTER: if (n > 0) show_logview(g_log_sel); break;
    case LV_KEY_ESC:   show_profiles(); break;
    default: if (k == 'd' && n > 0) { logsink_delete(g_log_sel); show_logs(); } break;
    }
}

/* ---------------- session menu + file browser (overlays over terminal) ---- */
static int       g_menu_sel = 0;
static int       g_file_sel = 0;
static char      g_files[64][96];
static int       g_file_isdir[64];
static int       g_files_n = 0;
static char      g_send_path[256];
static int       g_send_wait;        /* send dialog: 0 = fixed pace, 1 = wait-for-prompt */
static char      g_browse_dir[512];        /* current folder in the file browser */

static const char *filedir(void)
{
    const char *d = getenv("TERM_FILEDIR");
    if (d && *d) return d;
    d = getenv("HOME");
    return (d && *d) ? d : "/sdcard";
}

/* delete the whole overlay (backdrop + panel) and resume the terminal render */
static void kill_overlay(void)
{
    if (g_backdrop) { lv_obj_delete(g_backdrop); g_backdrop = NULL; }
    g_overlay = NULL;
    term_render_pause(0);
}

static void close_overlay(void)
{
    kill_overlay();
    g_scr = SCR_TERM;
}

static lv_obj_t *overlay_panel(int w, int h)
{
    kill_overlay();                  /* drop any previous overlay first */
    term_render_pause(1);            /* freeze the terminal so nothing bleeds through */

    /* Full-screen container that parents the panel (one delete frees both).
     * Transparent — the terminal stays visible OUTSIDE the panel frame; only the
     * panel itself is opaque. term_render_pause() keeps the cursor off the panel. */
    g_backdrop = lv_obj_create(g_root);
    lv_obj_remove_flag(g_backdrop, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(g_backdrop, 0, 0);
    lv_obj_set_style_radius(g_backdrop, 0, 0);
    lv_obj_set_style_pad_all(g_backdrop, 0, 0);
    lv_obj_set_style_bg_opa(g_backdrop, LV_OPA_TRANSP, 0);   /* see the terminal behind */
    lv_obj_set_size(g_backdrop, 320, 170);
    lv_obj_set_pos(g_backdrop, 0, 0);

    lv_obj_t *o = lv_obj_create(g_backdrop);   /* centered panel on the backdrop */
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x10101E), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_size(o, w, h);
    lv_obj_center(o);
    return o;
}

/* ---------------- quick-send macros (overlay on the live terminal) ----------------
 * Session Menu -> "Macros...": pick a saved one-line command and send it (+Enter).
 * n:new  e:edit  d:delete  — stored globally in term.conf (mac<i>.name/.text). */
static int g_mac_sel = 0, g_mac_edit = -1, g_mac_field = 0, g_mac_editing = 0;

static lv_obj_t *ovlabel(const lv_font_t *f, uint32_t color, int x, int y, const char *txt)
{
    return mklabel_on(g_overlay, f, color, x, y, txt);   /* same, parented to the overlay panel */
}

static void open_macros(void)
{
    g_scr = SCR_MACROS;
    g_overlay = overlay_panel(280, 150);
    ovlabel(ui_font(14), COL_TITLE, 8, 4, tr("Macros","マクロ"));
    int n = config_macro_count();
    if (g_mac_sel >= n) g_mac_sel = n > 0 ? n - 1 : 0;
    if (n == 0)
        ovlabel(ui_font(12), COL_DIM, 12, 30, tr("(empty — n:new)","(未登録 — n:新規)"));
    const int VIS = 7;                          /* rows that fit above the footer (y=132) */
    int start = g_mac_sel >= VIS ? g_mac_sel - VIS + 1 : 0;
    for (int i = start; i < n && i < start + VIS; i++) {
        const macro_t *m = config_macro(i);
        char row[64];
        snprintf(row, sizeof(row), "%-10.10s %.20s", m->name, m->text);
        ovlabel(ui_font(12), i == g_mac_sel ? COL_TEXT : COL_DIM, 12, 24 + (i - start) * 15, row);
    }
    if (start > 0)           ovlabel(ui_font(12), COL_DIM, 264, 24, LV_SYMBOL_UP);
    if (start + VIS < n)     ovlabel(ui_font(12), COL_DIM, 264, 24 + (VIS - 1) * 15, LV_SYMBOL_DOWN);
    ovlabel(ui_font(12), COL_DIM, 8, 132,
            tr("Enter:send n:new e:edit d:del ESC","Enter:送信 n:新規 e:編集 d:削除 ESC"));
}

static void open_macro_edit(int idx)
{
    g_mac_edit = idx;
    g_scr = SCR_MACRO_EDIT;
    g_overlay = overlay_panel(280, 110);
    macro_t *m = config_macro_mutable(idx);
    if (!m) { open_macros(); return; }
    ovlabel(ui_font(14), COL_TITLE, 8, 4, tr("Edit macro","マクロ編集"));
    const char *lab[2] = { tr("Name","名前"), tr("Text","コマンド") };
    const char *val[2] = { m->name, m->text };
    for (int i = 0; i < 2; i++) {
        ovlabel(ui_font(12), i == g_mac_field ? COL_TEXT : COL_DIM, 12, 28 + i * 18, lab[i]);
        char v[140];
        if (g_mac_editing && i == g_mac_field) snprintf(v, sizeof(v), "%s_", g_ebuf);
        else                                   snprintf(v, sizeof(v), "%s", val[i]);
        ovlabel(ui_font(12), i == g_mac_field ? COL_CYAN : COL_DIM, 76, 28 + i * 18, v);
    }
    ovlabel(ui_font(12), COL_DIM, 8, 90,
            tr("Enter:edit  ESC:done (empty=discard)","Enter:編集  ESC:完了 (空は破棄)"));
}

/* leave the macro editor: discard an empty macro (Enter would no-op on it and
 * phantom entries would otherwise be persisted by any later config_save), then
 * persist — so n/e/d all leave term.conf consistent without a separate 's'. */
static void macro_edit_done(void)
{
    macro_t *m = config_macro_mutable(g_mac_edit);
    if (m && !m->text[0]) {
        config_macro_delete(g_mac_edit);
        if (g_mac_sel >= config_macro_count()) g_mac_sel = config_macro_count() - 1;
        if (g_mac_sel < 0) g_mac_sel = 0;
    }
    config_save();
    open_macros();
}

static void macro_send(int idx)
{
    const macro_t *m = config_macro(idx);
    if (!m || !m->text[0] || !term_is_alive()) return;   /* never write to a dead/absent PTY */
    term_send_bytes(m->text, (int)strlen(m->text));
    term_send_bytes("\r", 1);                       /* run it */
}

static void key_macros(uint32_t k)
{
    int n = config_macro_count();
    switch (k) {
    case LV_KEY_UP:    if (g_mac_sel > 0) { g_mac_sel--; open_macros(); } break;
    case LV_KEY_DOWN:  if (g_mac_sel < n - 1) { g_mac_sel++; open_macros(); } break;
    case LV_KEY_ESC:   close_overlay(); break;
    case LV_KEY_ENTER: if (n > 0) { close_overlay(); macro_send(g_mac_sel); } break;
    default:
        if (k == 'n') { int i = config_macro_add(); if (i >= 0) { g_mac_sel = i; g_mac_field = 0; g_mac_editing = 0; open_macro_edit(i); } }
        else if (k == 'e' && n > 0) { g_mac_field = 0; g_mac_editing = 0; open_macro_edit(g_mac_sel); }
        else if (k == 'd' && n > 0) {
            config_macro_delete(g_mac_sel); config_save();
            if (g_mac_sel >= config_macro_count()) g_mac_sel = config_macro_count() - 1;
            if (g_mac_sel < 0) g_mac_sel = 0;
            open_macros();
        }
        break;
    }
}

static void key_macro_edit(uint32_t k)
{
    macro_t *m = config_macro_mutable(g_mac_edit);
    if (!m) { open_macros(); return; }
    char  *buf = g_mac_field == 0 ? m->name : m->text;
    size_t sz  = g_mac_field == 0 ? sizeof(m->name) : sizeof(m->text);

    if (g_mac_editing) {   /* inline text entry, same pattern as the profile editor */
        if (k == LV_KEY_ENTER)      { snprintf(buf, sz, "%s", g_ebuf); g_mac_editing = 0; open_macro_edit(g_mac_edit); }
        else if (k == LV_KEY_ESC)   { g_mac_editing = 0; open_macro_edit(g_mac_edit); }
        else if (k == LV_KEY_BACKSPACE) { size_t l = strlen(g_ebuf); if (l) g_ebuf[l-1] = 0; open_macro_edit(g_mac_edit); }
        else if (k >= 0x20 && k < 0x7f) { size_t l = strlen(g_ebuf); if (l < sizeof(g_ebuf)-1) { g_ebuf[l]=(char)k; g_ebuf[l+1]=0; } open_macro_edit(g_mac_edit); }
        return;
    }
    switch (k) {
    case LV_KEY_UP:    if (g_mac_field > 0) g_mac_field--; open_macro_edit(g_mac_edit); break;
    case LV_KEY_DOWN:  if (g_mac_field < 1) g_mac_field++; open_macro_edit(g_mac_edit); break;
    case LV_KEY_ENTER: snprintf(g_ebuf, sizeof(g_ebuf), "%s", buf); g_mac_editing = 1; open_macro_edit(g_mac_edit); break;
    case LV_KEY_ESC:   macro_edit_done(); break;
    default:
        if (k == 's') macro_edit_done();   /* kept as a habit alias for ESC */
        break;
    }
}

/* ---------------- terminal scrollback view ---------------- */
static void scroll_hint(int on)   /* bottom indicator while viewing history */
{
    if (on && !g_scrollhint) {
        g_scrollhint = mklabel(ui_font(12), COL_CYAN, 4, 154, "");
        lv_obj_set_style_bg_color(g_scrollhint, lv_color_hex(0x10101E), 0);
        lv_obj_set_style_bg_opa(g_scrollhint, LV_OPA_COVER, 0);
        lv_obj_set_width(g_scrollhint, 320);
        lv_label_set_text(g_scrollhint, tr("scrollback  (Alt+down / type = live)",
                                           "履歴表示中  (Alt+下 / 入力 = 最新)"));
    } else if (!on && g_scrollhint) {
        lv_obj_delete(g_scrollhint); g_scrollhint = NULL;
    }
}

/* ---- line-copy mode (Alt+c): highlight a line, Enter copies, Alt+v pastes ----
 * Internal one-line clipboard: pick up an IP / command / error from the output
 * (incl. scrollback history) and re-send it. Alt+c/Alt+v shadow Meta-c/Meta-v. */
static void term_alt_scroll(uint32_t base);
static char g_clip[520];               /* internal clipboard: one line */

static void copy_bar_place(void)
{
    if (!g_copybar) return;
    lv_obj_set_pos(g_copybar, 0, g_copy_row * g_ch);
    lv_obj_move_foreground(g_copybar);
    if (g_copyhint) lv_obj_move_foreground(g_copyhint);
}

static void copy_mode_off(void)
{
    if (g_copybar)  { lv_obj_delete(g_copybar);  g_copybar = NULL; }
    if (g_copyhint) { lv_obj_delete(g_copyhint); g_copyhint = NULL; }
    if (g_copy_row >= 0) term_render_pause(0);   /* resume live output */
    g_copy_row = -1;
}

static void copy_mode_on(void)
{
    if (g_copy_row >= 0) return;
    g_copy_row = g_rows - 1;
    term_render_pause(1);        /* freeze the frame we're selecting from */
    term_render_once();          /* ...but show the latest content first */
    g_copybar = lv_obj_create(g_root);
    lv_obj_remove_flag(g_copybar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(g_copybar, 0, 0);
    lv_obj_set_style_radius(g_copybar, 0, 0);
    lv_obj_set_style_bg_color(g_copybar, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_bg_opa(g_copybar, LV_OPA_30, 0);
    lv_obj_set_size(g_copybar, 320, g_ch);
    g_copyhint = mklabel(ui_font(12), COL_CYAN, 4, 154, "");
    lv_obj_set_style_bg_color(g_copyhint, lv_color_hex(0x10101E), 0);
    lv_obj_set_style_bg_opa(g_copyhint, LV_OPA_COVER, 0);
    lv_obj_set_width(g_copyhint, 320);
    lv_label_set_text(g_copyhint, tr("copy  " LV_SYMBOL_UP LV_SYMBOL_DOWN ":line  Enter:copy  ESC",
                                     "コピー  ↑↓:行  Enter:コピー  ESC"));
    copy_bar_place();
}

static void copy_mode_key(uint32_t k)
{
    int scrolled = 0;
    if (k & 0x20000000u) { term_alt_scroll(k & 0xFF); scrolled = 1; }   /* Alt+arrow pages history */
    else switch (k) {
    case LV_KEY_UP:
        if (g_copy_row > 0) g_copy_row--;
        else { term_scroll(+1); scrolled = 1; }  /* highlight at the top -> scroll history */
        break;
    case LV_KEY_DOWN:
        if (g_copy_row < g_rows - 1) g_copy_row++;
        else if (term_scroll_pos() > 0) { term_scroll(-1); scrolled = 1; }
        break;
    case LV_KEY_ENTER:
        term_copy_line(g_copy_row, g_clip, sizeof(g_clip));
        copy_mode_off();
        return;
    case LV_KEY_ESC:
        copy_mode_off();
        return;
    default:
        return;
    }
    if (scrolled) term_render_once();            /* repaint the scrolled frame (still paused) */
    copy_bar_place();                            /* keep the bar above the labels */
}

/* Alt + arrow scrolls the scrollback while staying in the live terminal. */
static void term_alt_scroll(uint32_t base)
{
    if      (base == LV_KEY_UP)    term_scroll(+1);
    else if (base == LV_KEY_DOWN)  term_scroll(-1);
    else if (base == LV_KEY_LEFT)  term_scroll(+(g_rows - 1));
    else if (base == LV_KEY_RIGHT) term_scroll(-(g_rows - 1));
    scroll_hint(term_scroll_pos() > 0);
}

/* menu items are built per open: BREAK appears only for a serial console */
enum { MI_SEND, MI_FONT, MI_MACROS, MI_LOG, MI_BREAK, MI_CLOSE, MI_BACK };
static int g_menu_items[8], g_menu_n;

static void build_menu(void)
{
    int n = 0;
    g_menu_items[n++] = MI_SEND;
    g_menu_items[n++] = MI_FONT;
    g_menu_items[n++] = MI_MACROS;
    g_menu_items[n++] = MI_LOG;
    const profile_t *p = config_get(g_cur_pidx);
    if (p && !strcmp(p->proto, "serial")) g_menu_items[n++] = MI_BREAK;
    g_menu_items[n++] = MI_CLOSE;
    g_menu_items[n++] = MI_BACK;
    g_menu_n = n;
}

static const char *menu_label(int id)
{
    switch (id) {
    case MI_SEND:   return tr("Send file...","ファイル流し込み...");
    case MI_MACROS: return tr("Macros...","マクロ...");
    case MI_LOG:    return tr("Toggle log","ログ ON/OFF");
    case MI_BREAK:  return tr("Send BREAK","BREAK送信");
    case MI_CLOSE:  return tr("Close session","切断");
    case MI_BACK:   return tr("Back","戻る");
    }
    return "";
}

static void open_menu(void)   /* uses current g_menu_sel (caller resets for a fresh open) */
{
    g_scr = SCR_MENU;
    build_menu();
    if (g_menu_sel >= g_menu_n) g_menu_sel = g_menu_n - 1;
    g_overlay = overlay_panel(200, 36 + g_menu_n * 18);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, ui_font(14), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4);
    lv_label_set_text(t, tr("Session","セッション"));
    for (int i = 0; i < g_menu_n; i++) {
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(l, ui_font(12), 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == g_menu_sel ? COL_TEXT : COL_DIM), 0);
        lv_obj_set_pos(l, 12, 24 + i * 18);
        char lbl[48];
        if (g_menu_items[i] == MI_FONT)
            snprintf(lbl, sizeof(lbl), tr("Font size  < %dpx >","文字サイズ < %dpx >"), SIZES[g_size_idx]);
        else
            snprintf(lbl, sizeof(lbl), "%s", menu_label(g_menu_items[i]));
        lv_label_set_text(l, lbl);
    }
}

static int path_is_dir(const char *full)
{
    struct stat st;
    return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
}

static void browse_up(void)   /* go to the parent folder */
{
    if (!strcmp(g_browse_dir, "/")) return;
    char *s = strrchr(g_browse_dir, '/');
    if (s && s != g_browse_dir) *s = '\0';
    else strcpy(g_browse_dir, "/");
    g_file_sel = 0; open_files();
}

static void open_files(void)   /* browses g_browse_dir; directories descend, files send */
{
    g_scr = SCR_FILES;
    g_files_n = 0;
    if (strcmp(g_browse_dir, "/") != 0) {       /* ".." to go up */
        g_file_isdir[g_files_n] = 1;
        snprintf(g_files[g_files_n++], sizeof(g_files[0]), "..");
    }
    DIR *d = opendir(g_browse_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && g_files_n < 64) {
            if (e->d_name[0] == '.') continue;
            char full[640];
            snprintf(full, sizeof(full), "%s/%s", g_browse_dir, e->d_name);
            g_file_isdir[g_files_n] = e->d_type == DT_DIR ||
                                      (e->d_type == DT_UNKNOWN && path_is_dir(full));
            snprintf(g_files[g_files_n++], sizeof(g_files[0]), "%s", e->d_name);
        }
        closedir(d);
    }
    if (g_file_sel >= g_files_n) g_file_sel = g_files_n > 0 ? g_files_n - 1 : 0;

    g_overlay = overlay_panel(300, 150);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, ui_font(14), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4);
    lv_label_set_text(t, tr("Send file","ファイル流し込み"));

    const int VIS = 6;
    int start = g_file_sel >= VIS ? g_file_sel - VIS + 1 : 0;
    for (int row = 0; row < VIS && start + row < g_files_n; row++) {
        int i = start + row;
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(l, ui_font(12), 0);
        uint32_t col = i == g_file_sel ? COL_TEXT : (g_file_isdir[i] ? COL_CYAN : COL_DIM);
        lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
        lv_obj_set_pos(l, 12, 24 + row * 18);
        char nm[110];
        snprintf(nm, sizeof(nm), "%s%s", g_files[i],
                 g_file_isdir[i] && strcmp(g_files[i], "..") ? "/" : "");
        lv_label_set_text(l, nm);
    }
    if (g_files_n == 0) {
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(l, 12, 24);
        lv_label_set_text(l, tr("(empty)","(空)"));
    }
}

static void key_files(uint32_t k)
{
    switch (k) {
    case LV_KEY_UP:    if (g_file_sel > 0) { g_file_sel--; open_files(); } break;
    case LV_KEY_DOWN:  if (g_file_sel < g_files_n - 1) { g_file_sel++; open_files(); } break;
    case LV_KEY_LEFT:  browse_up(); break;
    case LV_KEY_ENTER:
        if (g_files_n == 0) break;
        if (g_file_isdir[g_file_sel]) {
            if (!strcmp(g_files[g_file_sel], "..")) { browse_up(); break; }
            size_t l = strlen(g_browse_dir);
            snprintf(g_browse_dir + l, sizeof(g_browse_dir) - l, "%s%s",
                     l && g_browse_dir[l - 1] == '/' ? "" : "/", g_files[g_file_sel]);
            g_file_sel = 0; open_files();
        } else {
            size_t l = strlen(g_browse_dir);
            snprintf(g_send_path, sizeof(g_send_path), "%s%s%s", g_browse_dir,
                     l && g_browse_dir[l - 1] == '/' ? "" : "/", g_files[g_file_sel]);
            open_send();
        }
        break;
    case LV_KEY_ESC: open_menu(); break;
    }
}

static void open_send(void)   /* confirm dialog showing the auto-detected charset */
{
    g_scr = SCR_SEND;
    g_overlay = overlay_panel(290, 130);
    const char *enc = sendfile_detect(g_send_path);
    const char *bn = strrchr(g_send_path, '/'); bn = bn ? bn + 1 : g_send_path;

    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, ui_font(14), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4); lv_label_set_text(t, tr("Send file","ファイル流し込み"));

    struct { const char *k, *v; uint32_t c; } rows[] = {
        { tr("File","ファイル"),     bn,  COL_TEXT },
        { tr("Detected","文字コード"), enc, COL_AMBER },
        { tr("Send as","送出形式"),  tr("UTF-8  (auto-convert)","UTF-8 へ自動変換"), COL_CYAN },
        { tr("Pace","送出速度"),
          g_send_wait ? tr("< wait-for-prompt >","< プロンプト待ち >")
                      : tr("< fixed  10ms/line >","< 一定  10ms/行 >"), COL_CYAN },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *kk = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(kk, ui_font(12), 0);
        lv_obj_set_style_text_color(kk, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(kk, 12, 28 + i * 18); lv_label_set_text(kk, rows[i].k);
        lv_obj_t *vv = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(vv, ui_font(12), 0);
        lv_obj_set_style_text_color(vv, lv_color_hex(rows[i].c), 0);
        lv_obj_set_pos(vv, 88, 28 + i * 18); lv_label_set_text(vv, rows[i].v);
    }
    lv_obj_t *g = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(g, ui_font(12), 0);
    lv_obj_set_style_text_color(g, lv_color_hex(COL_DIM), 0);
    lv_obj_set_pos(g, 12, 108);
    lv_label_set_text(g, tr("Enter:send  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT ":pace  ESC",
                            "Enter:送出  ←→:速度  ESC:取消"));
}

static void key_send(uint32_t k)
{
    if (k == LV_KEY_ENTER) { sendfile_start(g_send_path, g_send_wait); close_overlay(); }
    else if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT) { g_send_wait = !g_send_wait; open_send(); }
    else if (k == LV_KEY_ESC) open_files();
}

static void key_menu(uint32_t k)
{
    switch (k) {
    case LV_KEY_UP:    if (g_menu_sel > 0) { g_menu_sel--; open_menu(); } break;
    case LV_KEY_DOWN:  if (g_menu_sel < g_menu_n - 1) { g_menu_sel++; open_menu(); } break;
    case LV_KEY_ESC:   close_overlay(); break;
    case LV_KEY_ENTER:
        switch (g_menu_items[g_menu_sel]) {
        case MI_SEND: g_file_sel = 0;
            snprintf(g_browse_dir, sizeof(g_browse_dir), "%s", filedir());
            open_files(); break;
        case MI_FONT:                                         /* Font size (live) */
            load_font_idx((g_size_idx + 1) % 3);
            term_resize(g_mono, g_cols, g_rows, g_cw, g_ch);
            open_menu();                                      /* refresh label */
            break;
        case MI_MACROS: g_mac_sel = 0; open_macros(); break;  /* overlay; term stays behind */
        case MI_LOG:
            if (logsink_is_open()) logsink_close();
            else logsink_open(config_get(g_cur_pidx) ? config_get(g_cur_pidx)->name : "session");
            close_overlay();
            break;
        case MI_BREAK:
            /* picocom escape C-a C-\ = send serial BREAK (router password recovery). */
            if (term_is_alive()) term_send_bytes("\x01\x1c", 2);
            close_overlay();
            break;
        case MI_CLOSE: close_overlay(); end_session(); break;
        case MI_BACK:  close_overlay(); break;
        }
        break;
    }
}

/* ---------------- confirm dialog (delete / VPN-fail) ---------------- */
static int  g_dlg_prev = SCR_PROFILES;
static int  g_dlg_sel = 0;
static uint32_t g_dlg_accent = 0x3AD8FF;
static void (*g_dlg_yes)(void) = NULL;
static char g_dlg_title[40], g_dlg_msg[96], g_dlg_yes_lbl[24];

static void render_dialog(void)
{
    uint32_t accent = g_dlg_accent;
    g_overlay = overlay_panel(244, 98);
    lv_obj_set_style_border_color(g_overlay, lv_color_hex(accent), 0);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, ui_font(14), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(accent), 0);
    lv_obj_set_pos(t, 10, 6); lv_label_set_text(t, g_dlg_title);
    lv_obj_t *m = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(m, ui_font(12), 0);
    lv_obj_set_style_text_color(m, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(m, 12, 32); lv_label_set_text(m, g_dlg_msg);
    const char *opts[2] = { g_dlg_yes_lbl, tr("Cancel","取消") };
    int ox = 12;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *o = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(o, ui_font(12), 0);
        lv_obj_set_style_text_color(o, lv_color_hex(i == g_dlg_sel ? COL_TEXT : COL_DIM), 0);
        if (i == g_dlg_sel) {
            lv_obj_set_style_bg_color(o, lv_color_hex(COL_HILITE), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
            lv_obj_set_style_pad_all(o, 3, 0);
        }
        lv_obj_set_pos(o, ox, 66); lv_label_set_text(o, opts[i]);
        ox += (int)strlen(opts[i]) * 8 + 28;
    }
}

static void open_dialog(int prev, uint32_t accent, const char *title,
                        const char *msg, const char *yeslbl, void (*onyes)(void))
{
    g_dlg_prev = prev; g_dlg_sel = 0; g_dlg_yes = onyes; g_dlg_accent = accent;
    snprintf(g_dlg_title, sizeof(g_dlg_title), "%s", title);
    snprintf(g_dlg_msg, sizeof(g_dlg_msg), "%s", msg);
    snprintf(g_dlg_yes_lbl, sizeof(g_dlg_yes_lbl), "%s", yeslbl);
    g_scr = SCR_DIALOG;
    render_dialog();
}

static void key_dialog(uint32_t k)
{
    switch (k) {
    case LV_KEY_LEFT:  g_dlg_sel = 0; render_dialog(); break;
    case LV_KEY_RIGHT: g_dlg_sel = 1; render_dialog(); break;
    case LV_KEY_ESC:   kill_overlay(); g_scr = g_dlg_prev; break;
    case LV_KEY_ENTER:
        if (g_dlg_sel == 0 && g_dlg_yes) { void (*cb)(void) = g_dlg_yes; kill_overlay(); cb(); }
        else { kill_overlay(); g_scr = g_dlg_prev; }
        break;
    }
}

/* ---------------- key dispatch ---------------- */
void key_cb(lv_event_t *e)
{
    uint32_t k = lv_event_get_key(e);
    if (g_copy_row >= 0 && (g_scr == SCR_TERM || g_scr == SCR_TERM_DISC)) {
        copy_mode_key(k);                                            /* line-copy mode eats keys */
        return;
    }
    switch (g_scr) {
    case SCR_TERM:
        if (k & 0x20000000u) {                                       /* Alt-tagged key */
            uint32_t b = k & 0xFF;
            if (b == LV_KEY_UP || b == LV_KEY_DOWN || b == LV_KEY_LEFT || b == LV_KEY_RIGHT) {
                term_alt_scroll(b);                                  /* Alt+arrow -> scrollback */
                break;
            }
            if (b == 'c') { copy_mode_on(); break; }                 /* line-copy (keeps scroll pos) */
            if (term_scroll_pos() > 0) { term_scroll_reset(); scroll_hint(0); }
            if (b == 'v') {                                          /* paste the internal clipboard */
                if (g_clip[0] && term_is_alive()) term_send_bytes(g_clip, (int)strlen(g_clip));
                break;
            }
            if (b >= 0x20 && b < 0x7f) {                             /* Alt+printable -> Meta
                                                                        (ESC-prefix, readline/emacs) */
                char m[2] = { 0x1b, (char)b };
                term_send_bytes(m, 2);
            }
            break;
        }
        if (term_scroll_pos() > 0) { term_scroll_reset(); scroll_hint(0); }  /* a keystroke -> live */
        term_feed_key(k);
        break;
    case SCR_TERM_DISC:                                            /* session ended: review output */
        if (k & 0x20000000u) {
            if ((k & 0xFF) == 'c') { copy_mode_on(); break; }      /* grab an error line */
            term_alt_scroll(k & 0xFF); break;                      /* Alt+arrow -> scroll history */
        }
        if (lv_tick_elaps(g_disc_tick) < 600) break;   /* grace: the flip is async (400ms poll) —
                                                          don't turn an in-flight keystroke into r/Enter */
        if (k == 'r' || k == 'R')                       reconnect_session();
        else if (k == LV_KEY_ENTER || k == LV_KEY_ESC)  end_session();
        break;
    case SCR_PROFILES: key_profiles(k); break;
    case SCR_EDITOR:   key_editor(k); break;
    case SCR_LOGS:     key_logs(k); break;
    case SCR_LOGVIEW:  key_logview(k); break;
    case SCR_MENU:     key_menu(k); break;
    case SCR_MACROS:   key_macros(k); break;
    case SCR_MACRO_EDIT: key_macro_edit(k); break;
    case SCR_FILES:    key_files(k); break;
    case SCR_SEND:     key_send(k); break;
    case SCR_DIALOG:   key_dialog(k); break;
    }
}

/* file-injection progress badge in the status-bar strip while a send runs */
static void sendprog_tick(lv_timer_t *t)
{
    (void)t;
    if (sendfile_active() && (g_scr == SCR_TERM || g_scr == SCR_TERM_DISC)) {
        if (!g_sendprog) {
            g_sendprog = mklabel(ui_font(12), COL_AMBER, 250, 156, "");
            lv_obj_set_style_bg_color(g_sendprog, lv_color_hex(0x0A0A10), 0);
            lv_obj_set_style_bg_opa(g_sendprog, LV_OPA_COVER, 0);
        }
        char s[16]; snprintf(s, sizeof(s), "SEND %d%%", sendfile_progress());
        lv_label_set_text(g_sendprog, s);
        lv_obj_move_foreground(g_sendprog);
    } else if (g_sendprog) {
        lv_obj_delete(g_sendprog); g_sendprog = NULL;
    }
}

static void watch_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_session_up || term_is_alive()) return;
    switch (g_scr) {   /* terminal + every overlay that floats above a live session */
    case SCR_TERM: case SCR_MENU: case SCR_MACROS: case SCR_MACRO_EDIT:
    case SCR_FILES: case SCR_SEND:
        kill_overlay();          /* no-op on SCR_TERM */
        enter_disconnected();    /* freeze output for review instead of bouncing to the list */
        break;
    }
}

void app_main(lv_obj_t *parent)
{
    g_root = parent;
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    config_load();
    g_lang = config_lang();
    show_profiles();
    g_watch = lv_timer_create(watch_cb, 400, NULL);
    lv_timer_create(statusbar_tick, 2000, NULL);   /* live clock / wifi / battery */
    lv_timer_create(sendprog_tick, 200, NULL);     /* file-injection progress badge */

#if defined(SSH_TERM_TEST_HOOKS)
    /* Headless test hooks — emulator/CI only; NOT compiled into device builds. */
    const char *el = getenv("UI_LANG"); if (el) g_lang = !strcmp(el, "ja");
    const char *ac = getenv("AUTO_CONNECT"); if (ac) connect_profile(atoi(ac));
    const char *ae = getenv("AUTO_EDIT");    if (ae) show_editor(atoi(ae));
    const char *al = getenv("AUTO_LOGS");    if (al) show_logs();
    const char *alv = getenv("AUTO_LOGVIEW"); if (alv) { logsink_list_count(); show_logview(atoi(alv)); }
    const char *alf = getenv("AUTO_LOGFIND"); if (alf && g_scr == SCR_LOGVIEW) {
        snprintf(g_logfind, sizeof(g_logfind), "%s", alf); logview_search(); logview_prompt(); }
    const char *as = getenv("AUTO_SENDFILE"); if (as) sendfile_start(as, getenv("AUTO_SENDWAIT") != NULL);
    const char *am = getenv("AUTO_MENU");     if (am) { g_menu_sel = 0; open_menu(); }
    const char *amc = getenv("AUTO_MACROS");  if (amc) { g_mac_sel = 0; open_macros(); }
    const char *ams = getenv("AUTO_MACRO_SEND"); if (ams) macro_send(atoi(ams));
    const char *ame = getenv("AUTO_MACRO_EDIT"); if (ame) open_macro_edit(atoi(ame));
    const char *acp = getenv("AUTO_COPYMODE"); if (acp) copy_mode_on();
    const char *af = getenv("AUTO_FILES");    if (af) { g_file_sel = 0;
        snprintf(g_browse_dir, sizeof(g_browse_dir), "%s", filedir()); open_files(); }
    const char *ad = getenv("AUTO_SENDDLG"); if (ad) { snprintf(g_send_path, sizeof(g_send_path), "%s", ad); open_send(); }
    const char *ak = getenv("AUTO_KEY"); if (ak) for (const char *p = ak; *p; p++) key_profiles((uint32_t)(unsigned char)*p);
#endif
}

void app_event(int type, void *data)
{
    (void)data;
    if (type == CZ_EV_EXIT_REQUEST) {
        /* Host is unloading us — e.g. the HOME button returns to the launcher.
         * Tear everything down from any screen so no PTY/log/VPN is left behind.
         * (term_destroy/vpn_down/logsink_close are idempotent.) */
        kill_overlay();
        sendfile_cancel();
        g_session_up = 0;
        term_destroy();
        logsink_close();
        vpn_down();
    } else if (type == CZ_EV_SIDE_KEY && g_scr == SCR_TERM) {
        if (g_copy_row >= 0) { copy_mode_off(); return; }   /* SIDE cancels copy mode, not open menu */
        g_menu_sel = 0; open_menu();
    }
}

#if defined(APP_EMU)
void ui_init(void) { app_main(lv_screen_active()); }
#endif
