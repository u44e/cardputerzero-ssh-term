/* sendfile.c — charset auto-detect (built-in heuristic) + iconv -> UTF-8 +
 * paced streaming into the PTY. Works without nkf/uchardet. */
#include "sendfile.h"
#include "term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

/* wait-for-prompt tuning: after a line is sent, hold the next one until output
 * has been quiet for SETTLE_MS (device finished echoing/replying); MAX_WAIT_MS
 * is the ceiling so a line that produces no echo can't stall the whole send. */
#define SF_SETTLE_MS  150
#define SF_MAX_WAIT_MS 2500

static struct {
    char   *buf;
    size_t  len, pos;
    lv_timer_t *tmr;
    int      wait;          /* 0 = fixed pace, 1 = wait-for-prompt (idle-settle) */
    int      saw_rx;        /* have we seen ANY reply to the current line yet? */
    unsigned last_seq;      /* term_rx_seq() at the previous tick */
    uint32_t idle_tick;     /* lv_tick when output last went quiet */
    uint32_t sent_tick;     /* lv_tick when the current line was sent */
} sf;

static int valid_utf8(const unsigned char *b, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = b[i];
        int extra;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return 0;
        if (i + extra >= n) return 0;
        for (int k = 1; k <= extra; k++)
            if ((b[i + k] & 0xC0) != 0x80) return 0;
        i += extra + 1;
    }
    return 1;
}

const char *sendfile_detect_buf(const unsigned char *b, size_t n)
{
    int has_high = 0;
    for (size_t i = 0; i < n; i++) if (b[i] >= 0x80) { has_high = 1; break; }
    if (!has_high) return "ascii";
    if (valid_utf8(b, n)) return "UTF-8";

    /* heuristic: count Shift_JIS vs EUC-JP double-byte sequences */
    int sjis = 0, euc = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        unsigned char c = b[i], d = b[i + 1];
        if (((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) &&
            ((d >= 0x40 && d <= 0x7E) || (d >= 0x80 && d <= 0xFC))) sjis++;
        if ((c >= 0xA1 && c <= 0xFE) && (d >= 0xA1 && d <= 0xFE)) euc++;
    }
    return (euc > sjis) ? "EUC-JP" : "Shift_JIS";
}

const char *sendfile_detect(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return "?";
    static unsigned char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return sendfile_detect_buf(buf, n);
}

static char *to_utf8(const char *in, size_t inlen, const char *from, size_t *outlen)
{
    if (!strcmp(from, "UTF-8") || !strcmp(from, "ascii")) {
        char *o = malloc(inlen + 1);
        memcpy(o, in, inlen); o[inlen] = 0; *outlen = inlen;
        return o;
    }
    iconv_t cd = iconv_open("UTF-8", from);
    if (cd == (iconv_t)-1) {
        char *o = malloc(inlen + 1);
        memcpy(o, in, inlen); o[inlen] = 0; *outlen = inlen;
        return o;
    }
    size_t cap = inlen * 4 + 16;
    char *out = malloc(cap);
    char *ip = (char *)in, *op = out;
    size_t il = inlen, ol = cap;
    iconv(cd, &ip, &il, &op, &ol);
    iconv_close(cd);
    *outlen = cap - ol;
    return out;
}

static void sf_send_line(void)
{
    size_t end = sf.pos;
    while (end < sf.len && sf.buf[end] != '\n') end++;
    if (end < sf.len) end++;   /* include the newline */
    term_send_bytes(sf.buf + sf.pos, (int)(end - sf.pos));
    sf.pos = end;
    sf.sent_tick = sf.idle_tick = lv_tick_get();
    sf.last_seq = term_rx_seq();
    sf.saw_rx = 0;             /* wait for the device to actually reply before settling */
}

static void sf_tick(lv_timer_t *t)
{
    if (!sf.buf || sf.pos >= sf.len) {
        lv_timer_delete(t); sf.tmr = NULL;
        free(sf.buf); sf.buf = NULL;
        return;
    }
    if (!sf.wait) { sf_send_line(); return; }   /* fixed pace: one line per tick */

    /* wait-for-prompt: send the next line once the reply has arrived AND settled.
     * If a line draws no reply at all, the MAX_WAIT ceiling releases it. */
    unsigned seq = term_rx_seq();
    if (seq != sf.last_seq) { sf.last_seq = seq; sf.idle_tick = lv_tick_get(); sf.saw_rx = 1; }
    if ((sf.saw_rx && lv_tick_elaps(sf.idle_tick) >= SF_SETTLE_MS) ||
        lv_tick_elaps(sf.sent_tick) >= SF_MAX_WAIT_MS)
        sf_send_line();
}

int sendfile_start(const char *path, int wait_prompt)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    char *raw = malloc((size_t)sz + 1);
    size_t rd = fread(raw, 1, (size_t)sz, f);
    fclose(f);
    raw[rd] = 0;

    const char *enc = sendfile_detect_buf((unsigned char *)raw, rd);
    size_t ol = 0;
    char *u = to_utf8(raw, rd, enc, &ol);
    free(raw);

    sendfile_cancel();
    sf.buf = u; sf.len = ol; sf.pos = 0;
    sf.wait = wait_prompt ? 1 : 0;
    sf.last_seq = term_rx_seq();
    sf.idle_tick = sf.sent_tick = lv_tick_get();
    /* 10ms in fixed-pace mode; 30ms is fine when we're just polling for idle */
    sf.tmr = lv_timer_create(sf_tick, sf.wait ? 30 : 10, NULL);
    return 0;
}

int sendfile_active(void) { return sf.tmr != NULL; }

int sendfile_progress(void)
{
    if (!sf.len) return 0;
    return (int)(sf.pos * 100 / sf.len);
}

void sendfile_cancel(void)
{
    if (sf.tmr) { lv_timer_delete(sf.tmr); sf.tmr = NULL; }
    if (sf.buf) { free(sf.buf); sf.buf = NULL; }
    sf.len = sf.pos = 0;
}
