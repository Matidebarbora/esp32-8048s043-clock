#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Builds a small weather icon (sun/moon / partly cloudy / cloud / rain /
// snow / storm), classified from a WMO weather code (see
// weather_code_description() in weather.h), sized to size x size px.
// Twemoji-derived image assets — see weather_icons_data.c. Returns the
// icon's image object; caller positions/aligns it as needed.
// is_night swaps the clear-sky sun icon for a moon icon — pass true only
// for a point-in-time "now" icon (never for a whole-day forecast summary,
// which has no single day/night state).
#ifdef __cplusplus
lv_obj_t *weather_icon_create(lv_obj_t *parent, int weather_code, lv_coord_t size, bool is_night = false);
#else
lv_obj_t *weather_icon_create(lv_obj_t *parent, int weather_code, lv_coord_t size, bool is_night);
#endif

#ifdef __cplusplus
}
#endif
