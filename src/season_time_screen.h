#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds and shows the Season Time (Winter/Summer DST) screen. Tapping back
// returns to parent_scr (the Settings menu).
void season_time_screen_show(lv_obj_t *parent_scr);

#ifdef __cplusplus
}
#endif
