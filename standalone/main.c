/* standalone/main.c — standalone-binary entry point for NetTerm.
 *
 * The AppStore/APPLauncher model fork/execs a standalone binary that owns the
 * display + keyboard (see docs/APPSTORE.md). This provides that main(): it sets
 * up an LVGL display + keyboard and calls the app's app_main()/app_event() — the
 * same ABI the dlopen host uses — so the existing app code (src/*.c) is reused
 * unchanged.
 *
 * This desktop build uses SDL (verifiable on macOS/Linux). The on-device build
 * swaps the SDL display/keyboard for lv_linux_fbdev + lv_evdev (see the porting
 * plan in docs/APPSTORE.md); the app_main() call is identical. */
#include <lvgl.h>
#include <SDL2/SDL.h>
#include <cz_app.h>          /* app_main / app_event / CZ_EV_* */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define LCD_W 320
#define LCD_H 170
#define SCALE 2             /* window scale for a desktop preview */

/* lv_sdl_keyboard.c (compiled in) exposes these; the app also overrides
 * lv_sdl_keyboard_create so full keys (Ctrl/Alt/Fn tags) bind. */
extern lv_indev_t *lv_sdl_keyboard_create(void);
extern void        lv_sdl_keyboard_handler(SDL_Event *e);

static uint32_t   g_lcd[LCD_W * LCD_H];   /* ARGB8888 framebuffer */
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static volatile int  g_quit;

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    int w = lv_area_get_width(area), h = lv_area_get_height(area);
    uint16_t *src = (uint16_t *)px;                 /* RGB565 */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = src[y * w + x];
            uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            g_lcd[(area->y1 + y) * LCD_W + area->x1 + x] =
                0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
    lv_display_flush_ready(d);
}

static void on_term(int sig) { (void)sig; g_quit = 1; }   /* launcher sends SIGTERM to quit */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("NetTerm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       LCD_W * SCALE, LCD_H * SCALE, SDL_WINDOW_SHOWN);
    g_ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, LCD_W, LCD_H);
    SDL_StartTextInput();

    lv_init();
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_flush_cb(disp, flush_cb);
    static uint8_t buf[LCD_W * LCD_H * 2];          /* RGB565 draw buffer */
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_sdl_keyboard_create();

    app_main(lv_screen_active());                   /* build NetTerm's UI on the active screen */

    Uint32 t = SDL_GetTicks();
    static Uint32 s_last_esc;
    while (!g_quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) g_quit = 1;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                Uint32 now = SDL_GetTicks();        /* double-ESC = SIDE key (Session Menu) */
                if (s_last_esc && now - s_last_esc < 450) { s_last_esc = 0; app_event(CZ_EV_SIDE_KEY, NULL); }
                else { s_last_esc = now; lv_sdl_keyboard_handler(&ev); }
            }
            else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP || ev.type == SDL_TEXTINPUT)
                lv_sdl_keyboard_handler(&ev);
        }
        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - t); t = now;
        lv_timer_handler();

        SDL_UpdateTexture(g_tex, NULL, g_lcd, LCD_W * 4);
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
        SDL_Delay(5);
    }

    app_event(CZ_EV_EXIT_REQUEST, NULL);            /* tear down PTY / log / VPN cleanly */
    SDL_Quit();
    return 0;
}
