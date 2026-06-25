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
#include "config.h"
#include "term.h"
#include "logsink.h"
#include "sendfile.h"
#include "vpn.h"
#include "ime.h"

#if defined(APP_EMU)
#define MONO_PATH "/System/Library/Fonts/Menlo.ttc"
#else
#define MONO_PATH "/usr/share/APPLaunch/share/font/LiberationMono-Regular.ttf"
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
       SCR_SEND, SCR_DIALOG };

static lv_obj_t   *g_root;
static int         g_scr = SCR_PROFILES;
static int         g_sel = 0;
static int         g_cur_pidx = 0;         /* profile of the live session */
static lv_group_t *g_grp;
static lv_obj_t   *g_cap;
static lv_timer_t *g_watch;

static const lv_font_t *g_mono;
static int g_cols = 45, g_rows = 12, g_cw = 7, g_ch = 12;
static lv_obj_t   *g_preedit;              /* IME preedit label (terminal) */
static lv_obj_t   *g_overlay;              /* menu/dialog/send/files overlay */
static lv_obj_t   *g_logview_ta;           /* log viewer scrollable textarea */

/* editor state */
static int  g_edit_idx = 0;
static int  g_field = 0;
static int  g_editing = 0;
static char g_ebuf[128];

/* logs browser state */
static int  g_log_sel = 0;

static void show_profiles(void);
static void show_editor(int idx);
static void show_logs(void);
static void connect_profile(int i);
static void open_send(void);
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
        g_fontcache[idx] = lv_freetype_font_create(MONO_PATH,
            LV_FREETYPE_FONT_RENDER_MODE_BITMAP, SIZES[idx], LV_FREETYPE_FONT_STYLE_NORMAL);
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

/* ---------------- helpers ---------------- */
static lv_obj_t *mklabel(const lv_font_t *f, uint32_t color, int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(g_root);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, txt);
    return l;
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

/* ---------------- profiles ---------------- */
static void show_profiles(void)
{
    g_scr = SCR_PROFILES;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    mklabel(&lv_font_montserrat_14, COL_TITLE, 8, 5, "Sessions");
    char cnt[16]; snprintf(cnt, sizeof(cnt), "%d", config_count());
    lv_obj_t *c = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0, cnt);
    lv_obj_align(c, LV_ALIGN_TOP_RIGHT, -8, 8);

    int top = 28, rh = 22, n = config_count();
    if (g_sel >= n) g_sel = n > 0 ? n - 1 : 0;
    for (int i = 0; i < n; i++) {
        const profile_t *p = config_get(i);
        int y = top + i * rh;
        if (i == g_sel) {
            mkrect(COL_HILITE, 0, y - 1, 320, rh - 2);
            mkrect(COL_CYAN, 0, y - 1, 3, rh - 2);
        }
        mklabel(&lv_font_montserrat_14, COL_TEXT, 12, y + 2, p->name);
        char meta[180];
        if (!strcmp(p->proto, "shell")) snprintf(meta, sizeof(meta), "shell");
        else snprintf(meta, sizeof(meta), "%s  %s%s%s:%s", p->proto,
                      p->user, p->user[0] ? "@" : "", p->host, p->port);
        lv_obj_t *m = mklabel(&lv_font_montserrat_12, COL_DIM, 150, y + 3, meta);
        if (!strcmp(p->proto, "telnet")) lv_obj_set_style_text_color(m, lv_color_hex(COL_AMBER), 0);
        if (p->log) { lv_obj_t *L = mklabel(&lv_font_montserrat_12, COL_AMBER, 0, 0, "L");
                      lv_obj_align(L, LV_ALIGN_TOP_RIGHT, -8, y + 3); }
    }

    lv_obj_t *guide = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0,
                              "Up/Dn  Enter:conn  e:edit  n:new  d:del  l:logs");
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

/* ---------------- editor ---------------- */
static const char *field_label(int f)
{
    switch (f) { case 0: return "Name"; case 1: return "Host"; case 2: return "Port";
    case 3: return "User"; case 4: return "Proto"; case 5: return "VPN"; case 6: return "Log";
    case 7: return "Size"; }
    return "?";
}
static void field_value(profile_t *p, int f, char *out, size_t n)
{
    switch (f) {
    case 0: snprintf(out, n, "%s", p->name); break;
    case 1: snprintf(out, n, "%s", p->host); break;
    case 2: snprintf(out, n, "%s", p->port); break;
    case 3: snprintf(out, n, "%s", p->user); break;
    case 4: snprintf(out, n, "< %s >", p->proto); break;
    case 5: snprintf(out, n, "%s", p->vpn[0] ? p->vpn : "(none)"); break;
    case 6: snprintf(out, n, "[%s] save session log", p->log ? "x" : " "); break;
    case 7: snprintf(out, n, "< %spx >", p->size[0] ? p->size : "12"); break;
    }
}
static int field_is_text(int f) { return f <= 3 || f == 5; }
static void field_set_text(profile_t *p, int f, const char *v)
{
    switch (f) {
    case 0: snprintf(p->name, sizeof(p->name), "%s", v); break;
    case 1: snprintf(p->host, sizeof(p->host), "%s", v); break;
    case 2: snprintf(p->port, sizeof(p->port), "%s", v); break;
    case 3: snprintf(p->user, sizeof(p->user), "%s", v); break;
    case 5: snprintf(p->vpn, sizeof(p->vpn), "%s", v); break;
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
    char title[64]; snprintf(title, sizeof(title), "Edit: %s", p ? p->name : "?");
    mklabel(&lv_font_montserrat_14, COL_TITLE, 8, 4, title);

    int top = 26, rh = 16;
    for (int f = 0; f < 8; f++) {
        int y = top + f * rh;
        if (f == g_field) { mkrect(COL_HILITE, 0, y, 320, rh - 1); mkrect(COL_CYAN, 0, y, 3, rh - 1); }
        mklabel(&lv_font_montserrat_12, f == g_field ? COL_TEXT : COL_DIM, 12, y + 1, field_label(f));
        char val[160];
        if (g_editing && f == g_field && field_is_text(f)) {
            snprintf(val, sizeof(val), "%s_", g_ebuf);
        } else {
            field_value(p, f, val, sizeof(val));
        }
        uint32_t vc = (f == 4 || f == 6 || f == 7) ? COL_CYAN : COL_TEXT;
        mklabel(&lv_font_montserrat_12, vc, 92, y + 1, val);
    }

    lv_obj_t *guide = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0,
        g_editing ? "type...  Enter:ok  ESC:cancel"
                  : "Up/Dn  Enter:edit  Left/Right:toggle  s:save  ESC:back");
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

static void editor_toggle(profile_t *p, int dir)
{
    if (g_field == 4) {
        const char *seq[] = { "ssh", "telnet", "shell" };
        int cur = 0;
        for (int i = 0; i < 3; i++) if (!strcmp(p->proto, seq[i])) cur = i;
        cur = (cur + (dir >= 0 ? 1 : 2)) % 3;
        snprintf(p->proto, sizeof(p->proto), "%s", seq[cur]);
    } else if (g_field == 6) {
        p->log = !p->log;
    } else if (g_field == 7) {
        int idx = (size_to_idx(p->size) + (dir >= 0 ? 1 : 2)) % 3;
        snprintf(p->size, sizeof(p->size), "%d", SIZES[idx]);
    }
}

static void key_editor(uint32_t k)
{
    profile_t *p = config_mutable(g_edit_idx);
    if (!p) { show_profiles(); return; }

    if (g_editing) {
        if (k == LV_KEY_ENTER)      { field_set_text(p, g_field, g_ebuf); g_editing = 0; show_editor(g_edit_idx); }
        else if (k == LV_KEY_ESC)   { g_editing = 0; show_editor(g_edit_idx); }
        else if (k == LV_KEY_BACKSPACE) { size_t l = strlen(g_ebuf); if (l) g_ebuf[l-1] = 0; show_editor(g_edit_idx); }
        else if (k >= 0x20 && k < 0x7f) { size_t l = strlen(g_ebuf); if (l < sizeof(g_ebuf)-1) { g_ebuf[l]=(char)k; g_ebuf[l+1]=0; } show_editor(g_edit_idx); }
        return;
    }
    switch (k) {
    case LV_KEY_UP:    if (g_field > 0) g_field--; show_editor(g_edit_idx); break;
    case LV_KEY_DOWN:  if (g_field < 7) g_field++; show_editor(g_edit_idx); break;
    case LV_KEY_LEFT:  editor_toggle(p, -1); show_editor(g_edit_idx); break;
    case LV_KEY_RIGHT: editor_toggle(p, +1); show_editor(g_edit_idx); break;
    case LV_KEY_ENTER:
        if (field_is_text(g_field)) { char v[160]; field_value(p, g_field, v, sizeof(v));
            snprintf(g_ebuf, sizeof(g_ebuf), "%s", g_field==5 && !strcmp(v,"(none)") ? "" : v);
            g_editing = 1; show_editor(g_edit_idx); }
        else editor_toggle(p, +1), show_editor(g_edit_idx);
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
        else if (k == 'd' && n > 0) {
            char m[80]; snprintf(m, sizeof(m), "Delete \"%s\" ?", config_get(g_sel)->name);
            open_dialog(SCR_PROFILES, COL_RED, "Confirm", m, "Delete", do_delete_cb);
        }
        else if (k == 'l') { g_log_sel = 0; show_logs(); }
        break;
    }
}

/* ---------------- terminal ---------------- */
static lv_obj_t *g_status;

static void add_status_bar(const profile_t *p, int logging)
{
    int by = 154;
    mkrect(0x0A0A10, 0, by, 320, 170 - by);
    mkrect(COL_DIM, 0, by, 320, 1);
    char s[96];
    snprintf(s, sizeof(s), "%s   CONNECTED%s   SIDE=menu",
             p->name, logging ? "   REC" : "");
    g_status = lv_label_create(g_root);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_12, 0);
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

    g_overlay = NULL;                /* any dialog overlay is cleaned by lv_obj_clean below */
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    if (p->log) logsink_open(p->name);
    term_create(g_root, argv, g_mono, g_cols, g_rows, g_cw, g_ch);
    add_status_bar(p, p->log);

    g_preedit = lv_label_create(g_root);
    lv_obj_set_style_text_font(g_preedit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_preedit, lv_color_hex(COL_AMBER), 0);
    lv_obj_align(g_preedit, LV_ALIGN_BOTTOM_LEFT, 4, -18);
    lv_label_set_text(g_preedit, "");
    lv_obj_add_flag(g_preedit, LV_OBJ_FLAG_HIDDEN);

    attach_capture();
    g_scr = SCR_TERM;
}

static void connect_profile(int i)
{
    const profile_t *p = config_get(i);
    if (!p) return;
    g_cur_pidx = i;
    if (p->vpn[0] && vpn_up(p->vpn) != 0) {     /* VPN failed -> ask */
        char m[80]; snprintf(m, sizeof(m), "%s did not come up", p->vpn);
        open_dialog(SCR_PROFILES, COL_RED, "VPN failed", m, "Connect anyway", do_connect_now);
        return;
    }
    do_connect_now();
}

/* ---------------- logs browser ---------------- */
static void show_logs(void)
{
    g_scr = SCR_LOGS;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    mklabel(&lv_font_montserrat_14, COL_TITLE, 8, 5, "Logs");

    int n = logsink_list_count();
    char cnt[24]; snprintf(cnt, sizeof(cnt), "%d files", n);
    lv_obj_t *c = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0, cnt);
    lv_obj_align(c, LV_ALIGN_TOP_RIGHT, -8, 8);

    int top = 28, rh = 20;
    if (g_log_sel >= n) g_log_sel = n > 0 ? n - 1 : 0;
    for (int i = 0; i < n && i < 6; i++) {
        int y = top + i * rh;
        if (i == g_log_sel) { mkrect(COL_HILITE, 0, y - 1, 320, rh - 2); mkrect(COL_CYAN, 0, y - 1, 3, rh - 2); }
        mklabel(&lv_font_montserrat_12, i == g_log_sel ? COL_TEXT : COL_DIM, 12, y + 1, logsink_list_name(i));
    }
    if (n == 0) mklabel(&lv_font_montserrat_12, COL_DIM, 12, 32, "(no logs yet)");

    lv_obj_t *guide = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0, "Up/Dn  Enter:view  d:del  ESC:back");
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    attach_capture();
}

static void show_logview(int i)
{
    g_scr = SCR_LOGVIEW;
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    if (!g_mono) load_font_idx(0);

    mklabel(&lv_font_montserrat_12, COL_TITLE, 6, 4, logsink_list_name(i));
    lv_obj_t *ta = lv_textarea_create(g_root);
    lv_obj_set_style_text_font(ta, g_mono, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(ta, 0, 0);
    lv_obj_set_size(ta, 320, 132);
    lv_obj_set_pos(ta, 0, 20);
    lv_textarea_set_cursor_click_pos(ta, false);
    lv_textarea_set_text(ta, "");

    static char buf[8192];
    logsink_read_stripped(i, buf, sizeof(buf));
    lv_textarea_set_text(ta, buf);
    g_logview_ta = ta;

    lv_obj_t *guide = mklabel(&lv_font_montserrat_12, COL_DIM, 0, 0, "Up/Dn:scroll  ESC:back");
    lv_obj_align(guide, LV_ALIGN_BOTTOM_LEFT, 8, -2);
    attach_capture();
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
static int       g_files_n = 0;
static char      g_send_path[256];

static const char *filedir(void)
{
    const char *d = getenv("TERM_FILEDIR");
    if (d && *d) return d;
    d = getenv("HOME");
    return (d && *d) ? d : "/sdcard";
}

static void close_overlay(void)
{
    if (g_overlay) { lv_obj_delete(g_overlay); g_overlay = NULL; }
    g_scr = SCR_TERM;
}

static lv_obj_t *overlay_panel(int w, int h)
{
    lv_obj_t *o = lv_obj_create(g_root);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x10101E), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(COL_CYAN), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_size(o, w, h);
    lv_obj_center(o);
    return o;
}

static const char *MENU[] = { "Send file (config)...", "Font size", "Toggle log",
                              "Close session", "Back" };
#define MENU_N 5

static void open_menu(void)   /* uses current g_menu_sel (caller resets for a fresh open) */
{
    g_scr = SCR_MENU;
    if (g_overlay) lv_obj_delete(g_overlay);
    g_overlay = overlay_panel(200, 124);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4);
    lv_label_set_text(t, "Session");
    for (int i = 0; i < MENU_N; i++) {
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == g_menu_sel ? COL_TEXT : COL_DIM), 0);
        lv_obj_set_pos(l, 12, 24 + i * 18);
        char lbl[48];
        if (i == 1) snprintf(lbl, sizeof(lbl), "Font size  < %dpx >", SIZES[g_size_idx]);
        else        snprintf(lbl, sizeof(lbl), "%s", MENU[i]);
        lv_label_set_text(l, lbl);
    }
}

static void open_files(void)
{
    g_scr = SCR_FILES; g_file_sel = 0;
    g_files_n = 0;
    DIR *d = opendir(filedir());
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && g_files_n < 64) {
            if (e->d_name[0] == '.') continue;
            snprintf(g_files[g_files_n++], sizeof(g_files[0]), "%s", e->d_name);
        }
        closedir(d);
    }
    if (g_overlay) lv_obj_delete(g_overlay);
    g_overlay = overlay_panel(300, 150);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4);
    lv_label_set_text(t, "Send file");
    for (int i = 0; i < g_files_n && i < 6; i++) {
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(i == g_file_sel ? COL_TEXT : COL_DIM), 0);
        lv_obj_set_pos(l, 12, 24 + i * 18);
        lv_label_set_text(l, g_files[i]);
    }
    if (g_files_n == 0) {
        lv_obj_t *l = lv_label_create(g_overlay);
        lv_obj_set_style_text_color(l, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(l, 12, 24);
        lv_label_set_text(l, "(empty)");
    }
}

static void key_files(uint32_t k)
{
    switch (k) {
    case LV_KEY_UP:    if (g_file_sel > 0) { g_file_sel--; open_files(); } break;
    case LV_KEY_DOWN:  if (g_file_sel < g_files_n - 1) { g_file_sel++; open_files(); } break;
    case LV_KEY_ENTER:
        if (g_files_n > 0) {
            snprintf(g_send_path, sizeof(g_send_path), "%s/%s", filedir(), g_files[g_file_sel]);
            open_send();
        }
        break;
    case LV_KEY_ESC: open_menu(); break;
    }
}

static void open_send(void)   /* confirm dialog showing the auto-detected charset */
{
    g_scr = SCR_SEND;
    if (g_overlay) lv_obj_delete(g_overlay);
    g_overlay = overlay_panel(290, 112);
    const char *enc = sendfile_detect(g_send_path);
    const char *bn = strrchr(g_send_path, '/'); bn = bn ? bn + 1 : g_send_path;

    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
    lv_obj_set_pos(t, 8, 4); lv_label_set_text(t, "Send file");

    struct { const char *k, *v; uint32_t c; } rows[] = {
        { "File",     bn,  COL_TEXT },
        { "Detected", enc, COL_AMBER },
        { "Send as",  "UTF-8  (auto-convert)", COL_CYAN },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *kk = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(kk, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(kk, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(kk, 12, 28 + i * 18); lv_label_set_text(kk, rows[i].k);
        lv_obj_t *vv = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(vv, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(vv, lv_color_hex(rows[i].c), 0);
        lv_obj_set_pos(vv, 88, 28 + i * 18); lv_label_set_text(vv, rows[i].v);
    }
    lv_obj_t *g = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(g, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g, lv_color_hex(COL_DIM), 0);
    lv_obj_set_pos(g, 12, 90); lv_label_set_text(g, "Enter Send   ESC Cancel");
}

static void key_send(uint32_t k)
{
    if (k == LV_KEY_ENTER) { sendfile_start(g_send_path); close_overlay(); }
    else if (k == LV_KEY_ESC) open_files();
}

static void key_menu(uint32_t k)
{
    switch (k) {
    case LV_KEY_UP:    if (g_menu_sel > 0) { g_menu_sel--; open_menu(); } break;
    case LV_KEY_DOWN:  if (g_menu_sel < MENU_N - 1) { g_menu_sel++; open_menu(); } break;
    case LV_KEY_ESC:   close_overlay(); break;
    case LV_KEY_ENTER:
        switch (g_menu_sel) {
        case 0: open_files(); break;                          /* Send file */
        case 1:                                               /* Font size (live) */
            load_font_idx((g_size_idx + 1) % 3);
            term_resize(g_mono, g_cols, g_rows, g_cw, g_ch);
            open_menu();                                      /* refresh label */
            break;
        case 2:                                               /* Toggle log */
            if (logsink_is_open()) logsink_close();
            else logsink_open(config_get(g_cur_pidx) ? config_get(g_cur_pidx)->name : "session");
            close_overlay();
            break;
        case 3: close_overlay(); term_destroy(); logsink_close(); vpn_down(); show_profiles(); break;
        case 4: close_overlay(); break;
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
    if (g_overlay) lv_obj_delete(g_overlay);
    g_overlay = overlay_panel(244, 98);
    lv_obj_set_style_border_color(g_overlay, lv_color_hex(accent), 0);
    lv_obj_t *t = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(accent), 0);
    lv_obj_set_pos(t, 10, 6); lv_label_set_text(t, g_dlg_title);
    lv_obj_t *m = lv_label_create(g_overlay);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(m, 12, 32); lv_label_set_text(m, g_dlg_msg);
    const char *opts[2] = { g_dlg_yes_lbl, "Cancel" };
    int ox = 12;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *o = lv_label_create(g_overlay);
        lv_obj_set_style_text_font(o, &lv_font_montserrat_12, 0);
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
    case LV_KEY_ESC:   lv_obj_delete(g_overlay); g_overlay = NULL; g_scr = g_dlg_prev; break;
    case LV_KEY_ENTER:
        if (g_dlg_sel == 0 && g_dlg_yes) { void (*cb)(void) = g_dlg_yes; g_overlay = NULL; cb(); }
        else { lv_obj_delete(g_overlay); g_overlay = NULL; g_scr = g_dlg_prev; }
        break;
    }
}

/* ---------------- IME preedit overlay ---------------- */
static void ime_commit_cb(const char *utf8, int n) { term_send_bytes(utf8, n); }

static void update_preedit(void)
{
    if (!g_preedit) return;
    if (ime_enabled()) {
        char s[340]; snprintf(s, sizeof(s), "\xE3\x81\x82 %s", ime_preedit()); /* あ + preedit */
        lv_label_set_text(g_preedit, s);
        lv_obj_clear_flag(g_preedit, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_preedit, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- key dispatch ---------------- */
void key_cb(lv_event_t *e)
{
    uint32_t k = lv_event_get_key(e);
    switch (g_scr) {
    case SCR_TERM:
        if (k == '`') { ime_toggle(); update_preedit(); break; }   /* IME on/off */
        if (ime_enabled() && ime_key(k, ime_commit_cb)) { update_preedit(); break; }
        term_feed_key(k);
        break;
    case SCR_PROFILES: key_profiles(k); break;
    case SCR_EDITOR:   key_editor(k); break;
    case SCR_LOGS:     key_logs(k); break;
    case SCR_LOGVIEW:
        if (k == LV_KEY_ESC) show_logs();
        else if (k == LV_KEY_DOWN && g_logview_ta) lv_obj_scroll_by(g_logview_ta, 0, -28, LV_ANIM_OFF);
        else if (k == LV_KEY_UP && g_logview_ta)   lv_obj_scroll_by(g_logview_ta, 0,  28, LV_ANIM_OFF);
        break;
    case SCR_MENU:     key_menu(k); break;
    case SCR_FILES:    key_files(k); break;
    case SCR_SEND:     key_send(k); break;
    case SCR_DIALOG:   key_dialog(k); break;
    }
}

static void watch_cb(lv_timer_t *t)
{
    (void)t;
    if (g_scr == SCR_TERM && !term_is_alive()) {
        term_destroy();
        logsink_close();
        vpn_down();
        show_profiles();
    }
}

void app_main(lv_obj_t *parent)
{
    g_root = parent;
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    config_load();
    show_profiles();
    g_watch = lv_timer_create(watch_cb, 400, NULL);

    const char *ac = getenv("AUTO_CONNECT"); if (ac) connect_profile(atoi(ac));
    const char *ae = getenv("AUTO_EDIT");    if (ae) show_editor(atoi(ae));
    const char *al = getenv("AUTO_LOGS");    if (al) show_logs();
    const char *as = getenv("AUTO_SENDFILE"); if (as) sendfile_start(as);
    const char *am = getenv("AUTO_MENU");     if (am) { g_menu_sel = 0; open_menu(); }
    const char *af = getenv("AUTO_FILES");    if (af) open_files();
    const char *ad = getenv("AUTO_SENDDLG"); if (ad) { snprintf(g_send_path, sizeof(g_send_path), "%s", ad); open_send(); }
    const char *ak = getenv("AUTO_KEY"); if (ak) for (const char *p = ak; *p; p++) key_profiles((uint32_t)(unsigned char)*p);
    const char *ai = getenv("AUTO_IME");
    if (ai) { ime_toggle();
        for (const char *p = ai; *p; p++) ime_key((uint32_t)(unsigned char)*p, ime_commit_cb);
        ime_key(LV_KEY_ENTER, ime_commit_cb); }
}

void app_event(int type, void *data)
{
    (void)data;
    if (type == CZ_EV_EXIT_REQUEST && g_scr == SCR_TERM) { term_destroy(); logsink_close(); }
    else if (type == CZ_EV_SIDE_KEY && g_scr == SCR_TERM) { g_menu_sel = 0; open_menu(); }
}

#if defined(APP_EMU)
void ui_init(void) { app_main(lv_screen_active()); }
#endif
