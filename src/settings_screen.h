#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds and shows the general app settings screen (currently: DST mode).
void settings_screen_show(lv_obj_t *clock_scr);

#ifdef __cplusplus
}
#endif
