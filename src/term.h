/* term.h — libvterm-backed terminal view on an LVGL parent: PTY child + reader
 * thread, per color-run labels (fg/bg/reverse/underline/bold), scrollback. */
#ifndef SSH_TERM_TERM_H
#define SSH_TERM_TERM_H

#include <lvgl.h>
#include <stddef.h>

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

/* Monotonic counter bumped on every PTY read — an "output activity" clock the
 * file-injection wait-for-prompt pacing polls to know when output has settled. */
unsigned term_rx_seq(void);

/* Tear down: stop reader thread, close PTY, free vterm. */
void term_destroy(void);

/* Freeze/resume rendering (and cursor raise) while an overlay covers the term. */
void term_render_pause(int paused);
void term_render_once(void);   /* force one repaint even while paused (copy-mode scroll) */

/* Scrollback view: +delta scrolls up into history, -delta toward live. */
void term_scroll(int delta);
void term_scroll_reset(void);   /* jump back to the live bottom */
int  term_scroll_pos(void);     /* current offset (0 = live) */

/* Copy the visible content line at screen_row (honors the scroll offset) as
 * UTF-8 into out (trailing blanks trimmed). Returns the length. */
int  term_copy_line(int screen_row, char *out, size_t outsz);

#endif /* SSH_TERM_TERM_H */
