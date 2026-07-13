/* ui_tune.h — UI パレットの一元化 + エミュレータ限定チューニングフック。
 *
 * cz-ui-tuner(Mac GUI)から UI_TUNE_FILE 環境変数で key=value ファイルを渡すと、
 * app_main 冒頭の ui_tune_load() が配色を実行時に上書きする。UI_TUNE_DUMP で
 * オブジェクトツリー(座標+解決済みスタイル)も書き出す(クリック選択用)。
 * ゲートは既存の SSH_TERM_TEST_HOOKS(build-emu.sh のみ定義)に便乗しており、
 * デバイスビルド(.deb)では全定数が整数定数式に畳まれフックも消える。
 *
 * 使い方: cz_app.h(LVGL)の後に include し、app_main 冒頭で ui_tune_load() を呼ぶ。
 * フォントは FreeType 動的ロードのため対象外(配色のみ)。
 */
#ifndef UI_TUNE_H
#define UI_TUNE_H

#ifdef SSH_TERM_TEST_HOOKS
#  define UI_TUNE_ENABLE
#endif

#ifdef UI_TUNE_ENABLE
#  define TUNABLE_U32(name, def)  static uint32_t name = (def);
#else
#  define TUNABLE_U32(name, def)  enum { name = (def) };
#endif

/* ── Palette(NetTerm 家系。docs/gen_mockups.py と対応)── */
TUNABLE_U32(COL_BG,      0x1A1A2E)   /* アプリ画面の背景(紺) */
TUNABLE_U32(COL_TITLE,   0x3AD8FF)   /* タイトル(シアン) */
TUNABLE_U32(COL_TEXT,    0xECECF2)
TUNABLE_U32(COL_DIM,     0xA2A2C0)
TUNABLE_U32(COL_HILITE,  0x2C2C52)   /* 選択帯 */
TUNABLE_U32(COL_CYAN,    0x3AD8FF)
TUNABLE_U32(COL_AMBER,   0xFFB82E)
TUNABLE_U32(COL_GREEN,   0x4CD96A)
TUNABLE_U32(COL_RED,     0xFF6B6B)
TUNABLE_U32(COL_TERM_BG, 0x000000)   /* 端末/ログ閲覧の背景(黒) */
TUNABLE_U32(COL_SURFACE, 0x10101E)   /* オーバーレイ面/ヒント帯 */
TUNABLE_U32(COL_STATUS,  0x0A0A10)   /* 下部ステータスバー */

#ifdef UI_TUNE_ENABLE

typedef struct { const char *n; const lv_font_t *f; } ui_tune_font_ent_t;
static const ui_tune_font_ent_t ui_tune_fonts[] = {
        { "montserrat_8",  &lv_font_montserrat_8  }, { "montserrat_10", &lv_font_montserrat_10 },
        { "montserrat_12", &lv_font_montserrat_12 }, { "montserrat_14", &lv_font_montserrat_14 },
        { "montserrat_16", &lv_font_montserrat_16 }, { "montserrat_18", &lv_font_montserrat_18 },
        { "montserrat_20", &lv_font_montserrat_20 }, { "montserrat_22", &lv_font_montserrat_22 },
        { "montserrat_24", &lv_font_montserrat_24 }, { "montserrat_26", &lv_font_montserrat_26 },
        { "montserrat_28", &lv_font_montserrat_28 }, { "montserrat_30", &lv_font_montserrat_30 },
        { "unscii_8",      &lv_font_unscii_8      }, { "unscii_16",     &lv_font_unscii_16     },
};
#define UI_TUNE_NFONTS (sizeof(ui_tune_fonts) / sizeof(ui_tune_fonts[0]))

static const char *ui_tune_font_name(const lv_font_t *f)
{
    for (size_t i = 0; i < UI_TUNE_NFONTS; i++)
        if (f == ui_tune_fonts[i].f) return ui_tune_fonts[i].n;
    return "?";
}

/* ── オブジェクトツリーのダンプ(UI_TUNE_DUMP=<path>)──
 * 形式: depth|class|x,y,w,h|text_color|bg_color|bg_opa|font|pl,pr,pt,pb,prow,pcol|radius|text
 * AUTO_* での画面遷移完了後に走るよう 250ms のワンショット(EMU_SHOT_MS より小さく)。 */
static void ui_tune_dump_walk(FILE *fp, lv_obj_t *o, int depth)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_obj_class_t *cls = lv_obj_get_class(o);
    const char *cn = "obj";
    const char *text = "";
    if (cls == &lv_label_class)         { cn = "label";    text = lv_label_get_text(o); }
    else if (cls == &lv_textarea_class) { cn = "textarea"; text = lv_textarea_get_text(o); }

    char tb[48]; size_t ti = 0;
    for (const char *p = text; *p && ti < sizeof tb - 1; p++)
        tb[ti++] = (*p == '|' || *p == '\n' || *p == '\r') ? ' ' : *p;
    tb[ti] = 0;

    fprintf(fp, "%d|%s|%d,%d,%d,%d|%06X|%06X|%d|%s|%d,%d,%d,%d,%d,%d|%d|%s\n",
            depth, cn,
            (int)a.x1, (int)a.y1, (int)lv_area_get_width(&a), (int)lv_area_get_height(&a),
            (unsigned)(lv_color_to_u32(lv_obj_get_style_text_color(o, 0)) & 0xFFFFFF),
            (unsigned)(lv_color_to_u32(lv_obj_get_style_bg_color(o, 0)) & 0xFFFFFF),
            (int)lv_obj_get_style_bg_opa(o, 0),
            ui_tune_font_name(lv_obj_get_style_text_font(o, 0)),
            (int)lv_obj_get_style_pad_left(o, 0),  (int)lv_obj_get_style_pad_right(o, 0),
            (int)lv_obj_get_style_pad_top(o, 0),   (int)lv_obj_get_style_pad_bottom(o, 0),
            (int)lv_obj_get_style_pad_row(o, 0),   (int)lv_obj_get_style_pad_column(o, 0),
            (int)lv_obj_get_style_radius(o, 0), tb);

    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++)
        ui_tune_dump_walk(fp, lv_obj_get_child(o, i), depth + 1);
}

static void ui_tune_dump_cb(lv_timer_t *t)
{
    const char *path = getenv("UI_TUNE_DUMP");
    if (path && *path) {
        FILE *fp = fopen(path, "w");
        if (fp) { ui_tune_dump_walk(fp, lv_screen_active(), 0); fclose(fp); }
    }
    lv_timer_delete(t);
}

#define UI_TUNE_KEYS(X) \
    X(COL_BG, "col_bg")           X(COL_TITLE, "col_title")   X(COL_TEXT, "col_text") \
    X(COL_DIM, "col_dim")         X(COL_HILITE, "col_hilite") X(COL_CYAN, "col_cyan") \
    X(COL_AMBER, "col_amber")     X(COL_GREEN, "col_green")   X(COL_RED, "col_red") \
    X(COL_TERM_BG, "col_term_bg") X(COL_SURFACE, "col_surface") X(COL_STATUS, "col_status")

static void ui_tune_load(void)
{
    if (getenv("UI_TUNE_DUMP"))
        lv_timer_create(ui_tune_dump_cb, 250, NULL);

    const char *path = getenv("UI_TUNE_FILE");
    if (!path || !*path) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128], key[40], val[40];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        if (sscanf(line, " %39[A-Za-z0-9_] = %39s", key, val) != 2) continue;
        if (0) {}
#define X(var, kname) else if (!strcmp(key, kname)) var = (uint32_t)strtoul(val, NULL, 0);
        UI_TUNE_KEYS(X)
#undef X
    }
    fclose(f);
}

#else
#define ui_tune_load() ((void)0)
#endif /* UI_TUNE_ENABLE */

#endif /* UI_TUNE_H */
