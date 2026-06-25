/* ime.h — in-app Japanese input. Phase: romaji -> hiragana (works standalone).
 * Kanji conversion via mozc (mozc_emacs_helper) is a device TODO; the preedit
 * + commit pipeline here is what mozc plugs into. */
#ifndef SSH_TERM_IME_H
#define SSH_TERM_IME_H

#include <stdint.h>

void        ime_toggle(void);
int         ime_enabled(void);
const char *ime_preedit(void);   /* current hiragana + pending romaji */
void        ime_reset(void);

/* Feed a key while IME is on. Calls commit(utf8,n) when text is committed.
 * Returns 1 if the key was consumed by the IME (do NOT pass to the PTY),
 * 0 if the caller should handle it (e.g. Enter with empty preedit). */
int  ime_key(uint32_t key, void (*commit)(const char *utf8, int n));

#endif /* SSH_TERM_IME_H */
