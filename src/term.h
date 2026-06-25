/* term.h — libvterm-backed terminal view on an LVGL parent.
 * Phase 1: local PTY (shell), per-row labels, monochrome, reader thread. */
#ifndef SSH_TERM_TERM_H
#define SSH_TERM_TERM_H

#include <lvgl.h>

/* Create the terminal: spawn argv[] on a PTY (cols x rows) and render it on
 * `parent` using `font` (monospace). cell_w/cell_h = grid pitch in px. */
void term_create(lv_obj_t *parent, const char *const argv[],
                 const lv_font_t *font, int cols, int rows,
                 int cell_w, int cell_h);

/* Forward an LVGL key (from LV_EVENT_KEY) to the PTY as terminal bytes. */
void term_feed_key(uint32_t key);

/* Write raw bytes to the PTY (used by the config file-injection feature). */
void term_send_bytes(const char *buf, int n);

/* Live-resize the grid/font (font-size change); resends PTY winsize. */
void term_resize(const lv_font_t *font, int cols, int rows, int cell_w, int cell_h);

/* 1 while the child/PTY is alive, 0 after it exits. */
int term_is_alive(void);

/* Tear down: stop reader thread, close PTY, free vterm. */
void term_destroy(void);

#endif /* SSH_TERM_TERM_H */
