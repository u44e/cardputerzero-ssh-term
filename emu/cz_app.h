/* cz_app.h — CardputerZero application ABI (emu include copy).
 * App exports exactly two C symbols:
 *   void app_main (lv_obj_t *parent);
 *   void app_event(int type, void *data);
 * Host owns LVGL; do NOT call lv_init().
 */
#ifndef CZ_APP_H
#define CZ_APP_H

#include <lvgl.h>

#if defined(_WIN32)
#define CZ_APP_EXPORT __declspec(dllexport)
#else
#define CZ_APP_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

CZ_APP_EXPORT void app_main(lv_obj_t *parent);
CZ_APP_EXPORT void app_event(int type, void *data);

#ifdef __cplusplus
}
#endif

enum {
    CZ_EV_PAUSE        = 1,
    CZ_EV_RESUME       = 2,
    CZ_EV_EXIT_REQUEST = 3,
    CZ_EV_SIDE_KEY     = 4,
};

#endif /* CZ_APP_H */
