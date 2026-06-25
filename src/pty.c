/* pty.c — self-written forkpty wrapper. Standard libc/POSIX only. */
#define _GNU_SOURCE          /* expose forkpty() in <pty.h> on glibc */
#define _DARWIN_C_SOURCE     /* expose forkpty() in <util.h> on macOS */
#include "pty.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#if defined(__APPLE__)
#include <util.h>   /* forkpty() on macOS */
#else
#include <pty.h>    /* forkpty() on Linux  */
/* Some build setups force-include libc headers before _GNU_SOURCE takes effect,
 * so <pty.h> may not declare forkpty(). Declare it explicitly (glibc signature). */
extern int forkpty(int *amaster, char *name,
                   const struct termios *termp, const struct winsize *winp);
#endif

int pty_open(pty_t *p, const char *const argv[], int cols, int rows)
{
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;

    int master = -1;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) return -1;

    if (pid == 0) {
        /* child */
        setenv("TERM", "xterm-256color", 1);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    p->fd = master;
    p->pid = pid;
    return 0;
}

int pty_read(pty_t *p, char *buf, size_t n)
{
    if (!p || p->fd < 0) return -1;
    return (int)read(p->fd, buf, n);
}

int pty_write(pty_t *p, const char *buf, size_t n)
{
    if (!p || p->fd < 0) return -1;
    return (int)write(p->fd, buf, n);
}

void pty_resize(pty_t *p, int cols, int rows)
{
    if (!p || p->fd < 0) return;
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    ioctl(p->fd, TIOCSWINSZ, &ws);
    if (p->pid > 0) kill(p->pid, SIGWINCH);
}

int pty_alive(pty_t *p, int *exit_status)
{
    if (!p || p->pid <= 0) return 0;
    int st = 0;
    pid_t r = waitpid(p->pid, &st, WNOHANG);
    if (r == 0) return 1;          /* still running */
    if (r == p->pid && exit_status) *exit_status = st;
    return 0;                      /* exited (or error) */
}

/* Hang up the child (SIGHUP) WITHOUT closing the master fd, so the reader
 * thread's blocking read() returns and it can be joined before we close the fd.
 * Closing a fd while another thread read()s it deadlocks on macOS. */
void pty_terminate(pty_t *p)
{
    if (p && p->pid > 0) kill(p->pid, SIGHUP);
}

void pty_close(pty_t *p)
{
    if (!p) return;
    if (p->fd >= 0) { close(p->fd); p->fd = -1; }
    if (p->pid > 0) { kill(p->pid, SIGHUP); waitpid(p->pid, NULL, 0); p->pid = -1; }
}
