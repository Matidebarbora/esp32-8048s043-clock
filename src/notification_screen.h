#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Full-screen notification history (most recent first), with a "remove all"
// action. Pushed on top of clock_scr; ctrl+home / the home icon returns.
void notification_screen_show(lv_obj_t *clock_scr);

#ifdef __cplusplus
}
#endif
