/* themes.c — terminal color themes managed in a JSON file.
 * /sdcard/themes.json (TERM_THEMES overrides): { "themes": [
 *   { "name": "green", "fg": "#4CD96A", "bg": "#000000" }, ... ] }
 * A missing file is seeded with the built-ins below so users can edit or add
 * themes; a broken/empty file falls back to the built-ins in memory. The
 * reader is a minimal flat scanner (per-theme objects, string values only) —
 * no JSON library on the device. */
#include "themes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_THEMES 16
#define NAME_MAX   15
#define DEF_FG     0x4CD96A   /* green-on-black, matches the native console */
#define DEF_BG     0x000000

typedef struct { char name[NAME_MAX + 1]; uint32_t fg, bg; } theme_t;

static const theme_t DEFAULTS[] = {
    { "green",  0x4CD96A, 0x000000 },
    { "amber",  0xFFB82E, 0x000000 },
    { "cyan",   0x3AD8FF, 0x000000 },
    { "white",  0xECECF2, 0x000000 },
    { "lcd",    0x0F380F, 0x9BBC0F },   /* Game Boy STN: dark green on yellow-green */
    { "pocket", 0x22281E, 0xC7CFA8 },   /* calculator LCD: dark grey on grey-green */
};

static theme_t s_themes[MAX_THEMES];
static int     s_n;

static const char *themes_path(void)
{
    const char *p = getenv("TERM_THEMES");
    return (p && *p) ? p : "/sdcard/themes.json";
}

static void use_defaults(void)
{
    s_n = (int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]));
    memcpy(s_themes, DEFAULTS, sizeof(DEFAULTS));
}

static void seed_file(void)   /* write the built-ins out for the user to edit */
{
    FILE *f = fopen(themes_path(), "w");
    if (!f) return;                       /* read-only fs — built-ins still active */
    fprintf(f, "{ \"themes\": [\n");
    for (int i = 0; i < s_n; i++)
        fprintf(f, "  { \"name\": \"%s\", \"fg\": \"#%06X\", \"bg\": \"#%06X\" }%s\n",
                s_themes[i].name, (unsigned)s_themes[i].fg, (unsigned)s_themes[i].bg,
                i + 1 < s_n ? "," : "");
    fprintf(f, "] }\n");
    fclose(f);
}

/* first "key":"value" in [s,e) -> out; 0 if absent/malformed */
static int jstr(const char *s, const char *e, const char *key, char *out, size_t osz)
{
    size_t kl = strlen(key);
    for (const char *p = s; p + kl + 3 < e; p++) {
        if (*p != '"' || strncmp(p + 1, key, kl) != 0 || p[1 + kl] != '"') continue;
        p += kl + 2;
        while (p < e && (*p == ' ' || *p == '\t')) p++;
        if (p >= e || *p != ':') return 0;
        p++;
        while (p < e && (*p == ' ' || *p == '\t')) p++;
        if (p >= e || *p != '"') return 0;
        p++;
        size_t i = 0;
        while (p < e && *p != '"' && i + 1 < osz) out[i++] = *p++;
        out[i] = 0;
        return 1;
    }
    return 0;
}

static uint32_t jcolor(const char *v, uint32_t dflt)   /* "#RRGGBB" / "0xRRGGBB" / "RRGGBB" */
{
    if (v[0] == '#') v++;
    else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
    char *end;
    unsigned long u = strtoul(v, &end, 16);
    return (end != v && *end == 0 && u <= 0xFFFFFF) ? (uint32_t)u : dflt;
}

void themes_load(void)
{
    s_n = 0;
    FILE *f = fopen(themes_path(), "r");
    if (!f) { use_defaults(); seed_file(); return; }
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    /* scan { ... } spans; each theme object is flat, so the span up to the
     * next '}' holds exactly one theme's keys (the root '{' folds into the
     * first theme's span, which is harmless) */
    const char *p = buf;
    while (s_n < MAX_THEMES && (p = strchr(p, '{')) != NULL) {
        const char *q = strchr(p + 1, '}');
        if (!q) break;
        char name[NAME_MAX + 1], col[16];
        if (jstr(p, q, "name", name, sizeof(name)) && name[0]) {
            theme_t *t = &s_themes[s_n++];
            snprintf(t->name, sizeof(t->name), "%s", name);
            t->fg = jstr(p, q, "fg", col, sizeof(col)) ? jcolor(col, DEF_FG) : DEF_FG;
            t->bg = jstr(p, q, "bg", col, sizeof(col)) ? jcolor(col, DEF_BG) : DEF_BG;
        }
        p = q + 1;
    }
    if (s_n == 0) use_defaults();          /* unparseable file — keep it, run on built-ins */
}

int themes_count(void) { return s_n; }

const char *themes_name(int i) { return (i >= 0 && i < s_n) ? s_themes[i].name : NULL; }

int themes_index(const char *name)
{
    if (name) for (int i = 0; i < s_n; i++)
        if (!strcmp(s_themes[i].name, name)) return i;
    return -1;
}

uint32_t themes_fg(const char *name)
{
    int i = themes_index(name);
    return i >= 0 ? s_themes[i].fg : DEF_FG;
}

uint32_t themes_bg(const char *name)
{
    int i = themes_index(name);
    return i >= 0 ? s_themes[i].bg : DEF_BG;
}
