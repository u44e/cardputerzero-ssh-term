/* logsink.h — raw session log tee + browser. Dir: $TERM_LOGDIR else /sdcard/logs. */
#ifndef SSH_TERM_LOGSINK_H
#define SSH_TERM_LOGSINK_H

#include <stddef.h>

void logsink_open(const char *profile_name);   /* start a <name>-<ts>.log */
void logsink_write(const char *buf, int n);     /* tee raw bytes (no-op if closed) */
int  logsink_is_open(void);
void logsink_close(void);

int         logsink_list_count(void);           /* rescans dir, mtime desc */
const char *logsink_list_name(int i);
int         logsink_read_stripped(int i, char *out, size_t n);  /* ANSI-stripped */
void        logsink_delete(int i);

#endif /* SSH_TERM_LOGSINK_H */
