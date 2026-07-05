/* port/evdev_kbd.c — evdev keyboard -> NetTerm's tagged key encoding.
 *
 * The app was written for the dlopen host's lv_sdl_keyboard, which delivers keys
 * pre-tagged: Ctrl = 0x40000000|byte, Alt = 0x20000000|key, F/nav = 0x10000000|
 * code. On the device the app fork/execs standalone and reads the raw keyboard
 * itself, so this LVGL keypad indev reproduces those tags from a Linux evdev
 * device — otherwise Ctrl/Alt/Fn/F-keys/Meta would not reach the terminal.
 *
 * The base + shift layout is standard US-ASCII against Linux keycodes. The
 * CardputerZero symbol (Fn) layer and the physical SIDE key are device-specific
 * and need the real keymap — see the TODOs (docs/APPSTORE.md phase 3).
 * SIDE key -> app_event(CZ_EV_SIDE_KEY) opens the Session Menu. */
#include <lvgl.h>
#include <cz_app.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>

/* Physical SIDE button keycode (opens the Session Menu). Override at build time
 * with -DAPP_SIDE_KEYCODE=<n>; KEY_MENU is a placeholder pending the device. */
#ifndef APP_SIDE_KEYCODE
#define APP_SIDE_KEYCODE KEY_MENU
#endif

/* US base / shifted ASCII, indexed by Linux keycode (0..127). 0 = not printable. */
static const char KMAP[128][2] = {
    [KEY_1]={'1','!'}, [KEY_2]={'2','@'}, [KEY_3]={'3','#'}, [KEY_4]={'4','$'},
    [KEY_5]={'5','%'}, [KEY_6]={'6','^'}, [KEY_7]={'7','&'}, [KEY_8]={'8','*'},
    [KEY_9]={'9','('}, [KEY_0]={'0',')'}, [KEY_MINUS]={'-','_'}, [KEY_EQUAL]={'=','+'},
    [KEY_Q]={'q','Q'}, [KEY_W]={'w','W'}, [KEY_E]={'e','E'}, [KEY_R]={'r','R'},
    [KEY_T]={'t','T'}, [KEY_Y]={'y','Y'}, [KEY_U]={'u','U'}, [KEY_I]={'i','I'},
    [KEY_O]={'o','O'}, [KEY_P]={'p','P'}, [KEY_LEFTBRACE]={'[','{'}, [KEY_RIGHTBRACE]={']','}'},
    [KEY_A]={'a','A'}, [KEY_S]={'s','S'}, [KEY_D]={'d','D'}, [KEY_F]={'f','F'},
    [KEY_G]={'g','G'}, [KEY_H]={'h','H'}, [KEY_J]={'j','J'}, [KEY_K]={'k','K'},
    [KEY_L]={'l','L'}, [KEY_SEMICOLON]={';',':'}, [KEY_APOSTROPHE]={'\'','"'}, [KEY_GRAVE]={'`','~'},
    [KEY_BACKSLASH]={'\\','|'},
    [KEY_Z]={'z','Z'}, [KEY_X]={'x','X'}, [KEY_C]={'c','C'}, [KEY_V]={'v','V'},
    [KEY_B]={'b','B'}, [KEY_N]={'n','N'}, [KEY_M]={'m','M'},
    [KEY_COMMA]={',','<'}, [KEY_DOT]={'.','>'}, [KEY_SLASH]={'/','?'}, [KEY_SPACE]={' ',' '},
};

typedef struct {
    int      fd;
    int      ctrl, alt, shift;   /* modifier state */
    uint32_t q[16]; int qh, qt;  /* decoded-key ring (press events) */
    int      releasing;          /* keypad indev: emit RELEASED after each PRESSED */
} evdev_kbd_t;

static uint32_t decode(evdev_kbd_t *k, int code)
{
    uint32_t nav = 0;
    switch (code) {
    case KEY_UP:    nav = LV_KEY_UP;    break;
    case KEY_DOWN:  nav = LV_KEY_DOWN;  break;
    case KEY_LEFT:  nav = LV_KEY_LEFT;  break;
    case KEY_RIGHT: nav = LV_KEY_RIGHT; break;
    case KEY_ENTER: case KEY_KPENTER: nav = LV_KEY_ENTER; break;
    case KEY_ESC:       nav = LV_KEY_ESC;       break;
    case KEY_BACKSPACE: nav = LV_KEY_BACKSPACE; break;
    case KEY_TAB:       nav = LV_KEY_NEXT;      break;   /* Tab */
    case KEY_HOME:      nav = LV_KEY_HOME;      break;
    case KEY_END:       nav = LV_KEY_END;       break;
    case KEY_DELETE:    nav = LV_KEY_DEL;       break;
    }
    if (nav) {   /* Alt+arrow -> scrollback (0x20000000 tag); others plain */
        if (k->alt && (nav == LV_KEY_UP || nav == LV_KEY_DOWN || nav == LV_KEY_LEFT || nav == LV_KEY_RIGHT))
            return 0x20000000u | nav;
        return nav;
    }

    /* F1-F12 / Insert / PageUp / PageDown -> 0x10000000 | code (1..15) */
    if (code >= KEY_F1 && code <= KEY_F10) return 0x10000000u | (uint32_t)(1 + code - KEY_F1);
    if (code == KEY_F11)      return 0x10000000u | 11;
    if (code == KEY_F12)      return 0x10000000u | 12;
    if (code == KEY_INSERT)   return 0x10000000u | 13;
    if (code == KEY_PAGEUP)   return 0x10000000u | 14;
    if (code == KEY_PAGEDOWN) return 0x10000000u | 15;

    /* printable (TODO device: CardputerZero Fn/symbol layer differs from US) */
    if (code < 0 || code >= 128) return 0;
    char c = KMAP[code][k->shift ? 1 : 0];
    if (!c) return 0;
    if (k->ctrl) {                                   /* Ctrl-letter -> control byte */
        char l = (char)(c | 0x20);
        if (l >= 'a' && l <= 'z') return 0x40000000u | (uint32_t)(l - 'a' + 1);
    }
    if (k->alt) return 0x20000000u | (uint32_t)(unsigned char)c;   /* Meta (ESC-prefix) */
    return (uint32_t)(unsigned char)c;
}

static void pump(evdev_kbd_t *k)
{
    struct input_event ev;
    while (read(k->fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (ev.type != EV_KEY) continue;
        int down = ev.value != 0;                    /* 1=press, 2=repeat, 0=release */
        switch (ev.code) {
        case KEY_LEFTCTRL: case KEY_RIGHTCTRL:  k->ctrl  = down; continue;
        case KEY_LEFTALT:  case KEY_RIGHTALT:   k->alt   = down; continue;
        case KEY_LEFTSHIFT:case KEY_RIGHTSHIFT: k->shift = down; continue;
        }
        if (ev.value == 0) continue;                 /* only press/repeat produce keys */
        if (ev.code == APP_SIDE_KEYCODE) { app_event(CZ_EV_SIDE_KEY, NULL); continue; }
        uint32_t key = decode(k, ev.code);
        if (key) {
            int nt = (k->qt + 1) & 15;
            if (nt != k->qh) { k->q[k->qt] = key; k->qt = nt; }   /* enqueue if room */
        }
    }
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    evdev_kbd_t *k = lv_indev_get_user_data(indev);
    if (k->releasing) { k->releasing = 0; data->state = LV_INDEV_STATE_RELEASED; return; }
    pump(k);
    if (k->qh != k->qt) {
        data->key = k->q[k->qh]; k->qh = (k->qh + 1) & 15;
        data->state = LV_INDEV_STATE_PRESSED;
        k->releasing = 1;
        data->continue_reading = (k->qh != k->qt);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

lv_indev_t *evdev_kbd_create(const char *dev)
{
    static evdev_kbd_t k;
    memset(&k, 0, sizeof k);
    k.fd = open(dev, O_RDONLY | O_NONBLOCK);
    if (k.fd < 0) return NULL;
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_user_data(indev, &k);
    return indev;
}
