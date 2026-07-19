/* themes.h — terminal color themes (name -> fg/bg), managed in themes.json. */
#ifndef SSH_TERM_THEMES_H
#define SSH_TERM_THEMES_H

#include <stdint.h>

/* Load /sdcard/themes.json (TERM_THEMES env overrides the path). A missing
 * file is seeded with the built-ins so users can edit/add themes; an
 * unreadable or empty file falls back to the built-ins in memory. */
void themes_load(void);

int         themes_count(void);
const char *themes_name(int i);               /* NULL if i out of range */
int         themes_index(const char *name);   /* -1 if unknown */
uint32_t    themes_fg(const char *name);      /* green if unknown */
uint32_t    themes_bg(const char *name);      /* black if unknown */

#endif /* SSH_TERM_THEMES_H */
