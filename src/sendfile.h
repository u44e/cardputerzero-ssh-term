/* sendfile.h — config file injection: auto-detect charset, convert to UTF-8,
 * stream into the active PTY at a gentle pace (for network-gear config paste). */
#ifndef SSH_TERM_SENDFILE_H
#define SSH_TERM_SENDFILE_H

#include <stddef.h>

/* Detect a file's encoding: "UTF-8" | "Shift_JIS" | "EUC-JP" | "ascii" | "?". */
const char *sendfile_detect(const char *path);

/* Read `path`, detect encoding, convert to UTF-8, send to the terminal.
 * wait_prompt=0: fixed pace (one line every 10ms); wait_prompt=1: hold each
 * line until device output settles (prompt-agnostic). Returns 0 ok, -1 error. */
int  sendfile_start(const char *path, int wait_prompt);

int  sendfile_active(void);   /* 1 while a send is in progress */
int  sendfile_progress(void); /* 0..100 percent */
void sendfile_cancel(void);

#endif /* SSH_TERM_SENDFILE_H */
