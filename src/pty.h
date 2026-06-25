/* pty.h — self-written forkpty wrapper (no third-party code).
 * Owns a child process on a PTY master fd. Used by the terminal core. */
#ifndef SSH_TERM_PTY_H
#define SSH_TERM_PTY_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    int   fd;   /* PTY master, -1 if closed */
    pid_t pid;  /* child pid, -1 if none */
} pty_t;

/* Launch argv[] on a fresh PTY sized cols x rows. 0 on success, -1 on failure. */
int  pty_open(pty_t *p, const char *const argv[], int cols, int rows);

/* Blocking read from the PTY. Returns bytes read, 0 on EOF, <0 on error. */
int  pty_read(pty_t *p, char *buf, size_t n);

/* Write bytes to the PTY (keyboard -> child). Returns bytes written or <0. */
int  pty_write(pty_t *p, const char *buf, size_t n);

/* Update the window size (TIOCSWINSZ) and notify the child (SIGWINCH). */
void pty_resize(pty_t *p, int cols, int rows);

/* 1 = child still running, 0 = exited (fills *exit_status if non-NULL). */
int  pty_alive(pty_t *p, int *exit_status);

/* Hang up the child (SIGHUP) but keep the fd open — lets a blocking reader
 * thread unblock (read returns EOF) so it can be joined before pty_close(). */
void pty_terminate(pty_t *p);

/* Close the PTY, hang up and reap the child. */
void pty_close(pty_t *p);

#endif /* SSH_TERM_PTY_H */
