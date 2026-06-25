/* ime.c — romaji -> hiragana conversion + preedit/commit pipeline.
 * Greedy longest-match with sokuon (っ) and hatsuon (ん) handling.
 * Kanji conversion via mozc_emacs_helper is a device TODO (see ime_convert). */
#include "ime.h"

#include <string.h>
#include <stdio.h>

/* LVGL key codes (stable values) — avoid pulling lvgl into this module */
#define LV_KEY_ENTER     10
#define LV_KEY_ESC       27
#define LV_KEY_BACKSPACE  8

typedef struct { const char *r, *k; } rk_t;
static const rk_t TBL[] = {
    {"kya","きゃ"},{"kyu","きゅ"},{"kyo","きょ"},{"sha","しゃ"},{"shu","しゅ"},{"sho","しょ"},
    {"sya","しゃ"},{"syu","しゅ"},{"syo","しょ"},{"cha","ちゃ"},{"chu","ちゅ"},{"cho","ちょ"},
    {"tya","ちゃ"},{"tyu","ちゅ"},{"tyo","ちょ"},{"nya","にゃ"},{"nyu","にゅ"},{"nyo","にょ"},
    {"hya","ひゃ"},{"hyu","ひゅ"},{"hyo","ひょ"},{"mya","みゃ"},{"myu","みゅ"},{"myo","みょ"},
    {"rya","りゃ"},{"ryu","りゅ"},{"ryo","りょ"},{"gya","ぎゃ"},{"gyu","ぎゅ"},{"gyo","ぎょ"},
    {"jya","じゃ"},{"jyu","じゅ"},{"jyo","じょ"},{"bya","びゃ"},{"byu","びゅ"},{"byo","びょ"},
    {"pya","ぴゃ"},{"pyu","ぴゅ"},{"pyo","ぴょ"},
    {"shi","し"},{"chi","ち"},{"tsu","つ"},
    {"ja","じゃ"},{"ju","じゅ"},{"jo","じょ"},
    {"ka","か"},{"ki","き"},{"ku","く"},{"ke","け"},{"ko","こ"},
    {"sa","さ"},{"si","し"},{"su","す"},{"se","せ"},{"so","そ"},
    {"ta","た"},{"ti","ち"},{"tu","つ"},{"te","て"},{"to","と"},
    {"na","な"},{"ni","に"},{"nu","ぬ"},{"ne","ね"},{"no","の"},
    {"ha","は"},{"hi","ひ"},{"hu","ふ"},{"fu","ふ"},{"he","へ"},{"ho","ほ"},
    {"ma","ま"},{"mi","み"},{"mu","む"},{"me","め"},{"mo","も"},
    {"ya","や"},{"yu","ゆ"},{"yo","よ"},
    {"ra","ら"},{"ri","り"},{"ru","る"},{"re","れ"},{"ro","ろ"},
    {"wa","わ"},{"wo","を"},{"nn","ん"},
    {"ga","が"},{"gi","ぎ"},{"gu","ぐ"},{"ge","げ"},{"go","ご"},
    {"za","ざ"},{"zi","じ"},{"ji","じ"},{"zu","ず"},{"ze","ぜ"},{"zo","ぞ"},
    {"da","だ"},{"di","ぢ"},{"du","づ"},{"de","で"},{"do","ど"},
    {"ba","ば"},{"bi","び"},{"bu","ぶ"},{"be","べ"},{"bo","ぼ"},
    {"pa","ぱ"},{"pi","ぴ"},{"pu","ぷ"},{"pe","ぺ"},{"po","ぽ"},
    {"a","あ"},{"i","い"},{"u","う"},{"e","え"},{"o","お"},{"-","ー"},
};
#define TBL_N (int)(sizeof(TBL)/sizeof(TBL[0]))

static struct {
    int  on;
    char pend[16];    /* pending romaji */
    char kana[256];   /* committed hiragana (preedit) */
    char disp[300];   /* kana + pend, for display */
} g;

static int is_cons(char c)
{
    return c && !strchr("aiueon", c) && c >= 'a' && c <= 'z';
}

static void emit(const char *k)
{
    if (strlen(g.kana) + strlen(k) < sizeof(g.kana) - 1) strcat(g.kana, k);
}

static void process(void)
{
    int prog = 1;
    while (prog && g.pend[0]) {
        prog = 0;
        size_t L = strlen(g.pend);
        /* sokuon: double consonant (not n) */
        if (L >= 2 && g.pend[0] == g.pend[1] && is_cons(g.pend[0])) {
            emit("っ"); memmove(g.pend, g.pend + 1, L); prog = 1; continue;
        }
        /* hatsuon: n + consonant(not y) */
        if (L >= 2 && g.pend[0] == 'n' && is_cons(g.pend[1]) && g.pend[1] != 'y') {
            emit("ん"); memmove(g.pend, g.pend + 1, L); prog = 1; continue;
        }
        for (int len = 3; len >= 1 && !prog; len--) {
            if ((int)L < len) continue;
            for (int i = 0; i < TBL_N; i++) {
                if ((int)strlen(TBL[i].r) == len && !strncmp(g.pend, TBL[i].r, len)) {
                    emit(TBL[i].k);
                    memmove(g.pend, g.pend + len, L - len + 1);
                    prog = 1; break;
                }
            }
        }
    }
}

void ime_toggle(void) { g.on = !g.on; ime_reset(); }
int  ime_enabled(void) { return g.on; }

void ime_reset(void) { g.pend[0] = 0; g.kana[0] = 0; }

const char *ime_preedit(void)
{
    snprintf(g.disp, sizeof(g.disp), "%s%s", g.kana, g.pend);
    return g.disp;
}

/* flush a trailing lone 'n' to ん */
static void flush_n(void)
{
    if (!strcmp(g.pend, "n")) { emit("ん"); g.pend[0] = 0; }
}

int ime_key(uint32_t key, void (*commit)(const char *utf8, int n))
{
    if (key == LV_KEY_ENTER) {
        flush_n();
        if (g.kana[0] || g.pend[0]) {
            process();
            commit(g.kana, (int)strlen(g.kana));
            ime_reset();
            return 1;
        }
        return 0;   /* empty preedit -> let terminal send newline */
    }
    if (key == LV_KEY_ESC) {
        if (g.kana[0] || g.pend[0]) { ime_reset(); return 1; }
        return 0;
    }
    if (key == LV_KEY_BACKSPACE) {
        size_t pl = strlen(g.pend);
        if (pl) { g.pend[pl - 1] = 0; return 1; }
        size_t kl = strlen(g.kana);
        if (kl) {   /* drop last UTF-8 char */
            size_t i = kl - 1;
            while (i > 0 && (g.kana[i] & 0xC0) == 0x80) i--;
            g.kana[i] = 0; return 1;
        }
        return 0;
    }
    if (key == ' ') {
        flush_n();
        if (g.kana[0] || g.pend[0]) {
            process();
            commit(g.kana, (int)strlen(g.kana));
            ime_reset();
            return 1;
        }
        return 0;   /* empty -> pass space through */
    }
    if (key >= 'a' && key <= 'z') {
        size_t pl = strlen(g.pend);
        if (pl < sizeof(g.pend) - 1) { g.pend[pl] = (char)key; g.pend[pl + 1] = 0; }
        process();
        return 1;
    }
    /* other key: commit what we have, then let caller handle the key */
    flush_n();
    if (g.kana[0]) { commit(g.kana, (int)strlen(g.kana)); ime_reset(); }
    return 0;
}
