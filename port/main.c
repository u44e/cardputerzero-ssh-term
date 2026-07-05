/* port/main.c — standalone-binary entry for the CardputerZero AppStore build.
 *
 * The AppStore fork/execs a standalone binary that owns the display + keyboard.
 * This main() sets up an LVGL display + input and calls the existing
 * app_main(lv_screen_active()) / app_event() ABI, so src/*.c is reused as-is.
 *
 *   default        : off-screen memory display — headless build/run verification
 *   -DPORT_FBDEV   : lv_linux_fbdev + lv_evdev — the on-device build
 *
 * Exit: the launcher sends SIGTERM (Home / long-ESC), which triggers
 * app_event(CZ_EV_EXIT_REQUEST) for a clean PTY/log/VPN teardown. */
#include <lvgl.h>
#include <cz_app.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define LCD_W 320
#define LCD_H 170

static volatile sig_atomic_t g_quit;
static void on_term(int s) { (void)s; g_quit = 1; }

static uint32_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

#if !defined(PORT_FBDEV)
/* headless: render into a memory buffer we never present (verify the app runs) */
static void null_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;
    lv_display_flush_ready(d);
}
#endif

int main(void)
{
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    lv_init();

#if defined(PORT_FBDEV)
    /* On-device: framebuffer + evdev keypad. Respect LV_LINUX_FBDEV_DEVICE
     * (the CardputerZero LCD may be /dev/fb1, not /dev/fb0). */
    lv_display_t *disp = lv_linux_fbdev_create();
    const char *fb = getenv("LV_LINUX_FBDEV_DEVICE");
    lv_linux_fbdev_set_file(disp, fb && *fb ? fb : "/dev/fb1");
    const char *kbd = getenv("APP_KEY_INPUT_DEVICE");
    lv_indev_t *in = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, kbd && *kbd ? kbd : "/dev/input/event0");
    (void)in;
    /* TODO(device): the app expects Ctrl/Alt/Fn/F-key/Meta tags (0x40/0x20/0x1C
     * bits) from the host keyboard driver. lv_evdev delivers plain LVGL keys, so
     * a modifier-aware evdev filter must re-apply those tags — needs the
     * CardputerZero keymap (Fn layer, SIDE key). See docs/APPSTORE.md phase 3. */
#else
    static uint8_t buf[LCD_W * LCD_H * 2];          /* RGB565 */
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_flush_cb(disp, null_flush);
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#endif
    (void)disp;

    app_main(lv_screen_active());                   /* build NetTerm's UI (src/main.c) */

    uint32_t t = now_ms();
    while (!g_quit) {
        uint32_t n = now_ms();
        lv_tick_inc(n - t); t = n;
        lv_timer_handler();
        usleep(5000);
    }

    app_event(CZ_EV_EXIT_REQUEST, NULL);            /* clean teardown */
    return 0;
}
