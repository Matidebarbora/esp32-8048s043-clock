#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds and shows the Dimmer screen (OFF/ON/AUTO + schedule for AUTO).
// Tapping back returns to parent_scr (the Settings menu).
void dimmer_screen_show(lv_obj_t *parent_scr);

#ifdef __cplusplus
}
#endif
