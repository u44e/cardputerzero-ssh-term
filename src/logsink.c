/* logsink.c — raw session log tee + browser + ANSI-stripped reader. */
#include "logsink.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LIST 64

static FILE *s_fp = NULL;

static char s_names[MAX_LIST][128];
static int  s_list_n = 0;

static const char *logdir(void)
{
    const char *d = getenv("TERM_LOGDIR");
    return (d && *d) ? d : "/sdcard/logs";
}

static void sanitize(const char *in, char *out, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < n; i++) {
        char c = in[i];
        out[j++] = (c == '/' || c == ' ' || c == ':') ? '_' : c;
    }
    out[j] = 0;
}

void logsink_open(const char *profile_name)
{
    logsink_close();
    mkdir(logdir(), 0755);
    char safe[64]; sanitize(profile_name ? profile_name : "session", safe, sizeof(safe));
    char ts[32];
    time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tmv);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s-%s.log", logdir(), safe, ts);
    s_fp = fopen(path, "ab");
}

void logsink_write(const char *buf, int n)
{
    if (s_fp && n > 0) { fwrite(buf, 1, (size_t)n, s_fp); fflush(s_fp); }
}

int logsink_is_open(void) { return s_fp != NULL; }

void logsink_close(void)
{
    if (s_fp) { fclose(s_fp); s_fp = NULL; }
}

/* ---- browser ---- */
static int cmp_mtime_desc(const void *a, const void *b)
{
    const char *na = (const char *)a, *nb = (const char *)b;
    char pa[256], pb[256];
    snprintf(pa, sizeof(pa), "%s/%s", logdir(), na);
    snprintf(pb, sizeof(pb), "%s/%s", logdir(), nb);
    struct stat sa, sb; sa.st_mtime = sb.st_mtime = 0;
    stat(pa, &sa); stat(pb, &sb);
    if (sa.st_mtime < sb.st_mtime) return 1;
    if (sa.st_mtime > sb.st_mtime) return -1;
    return 0;
}

int logsink_list_count(void)
{
    s_list_n = 0;
    DIR *d = opendir(logdir());
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) && s_list_n < MAX_LIST) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".log"))
            snprintf(s_names[s_list_n++], sizeof(s_names[0]), "%s", e->d_name);
    }
    closedir(d);
    qsort(s_names, s_list_n, sizeof(s_names[0]), cmp_mtime_desc);
    return s_list_n;
}

const char *logsink_list_name(int i)
{
    if (i < 0 || i >= s_list_n) return "";
    return s_names[i];
}

/* strip CSI (ESC[...letter) and OSC (ESC]...BEL/ST); drop CR */
int logsink_read_stripped(int i, char *out, size_t n)
{
    out[0] = 0;
    if (i < 0 || i >= s_list_n) return -1;
    char path[256]; snprintf(path, sizeof(path), "%s/%s", logdir(), s_names[i]);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t j = 0; int c;
    enum { NORMAL, ESC, CSI, OSC } st = NORMAL;
    while ((c = fgetc(f)) != EOF && j + 1 < n) {
        switch (st) {
        case NORMAL:
            if (c == 0x1b) st = ESC;
            else if (c == '\r') {}
            else out[j++] = (char)c;
            break;
        case ESC:
            if (c == '[') st = CSI;
            else if (c == ']') st = OSC;
            else st = NORMAL;
            break;
        case CSI:
            if (c >= 0x40 && c <= 0x7e) st = NORMAL;   /* final byte */
            break;
        case OSC:
            if (c == 0x07) st = NORMAL;                 /* BEL */
            else if (c == 0x1b) st = ESC;               /* ST start */
            break;
        }
    }
    out[j] = 0;
    fclose(f);
    return (int)j;
}

void logsink_delete(int i)
{
    if (i < 0 || i >= s_list_n) return;
    char path[256]; snprintf(path, sizeof(path), "%s/%s", logdir(), s_names[i]);
    unlink(path);
}
