#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds and shows the 5-day forecast screen (reads the cached data from
// weather_get_last() — does not trigger a new network request).
void forecast_screen_show(lv_obj_t *clock_scr);

#ifdef __cplusplus
}
#endif
