#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds a small vector-drawn weather icon (sun / partly cloudy / cloud /
// rain / snow / storm), classified from a WMO weather code (see
// weather_code_description() in weather.h), sized to size x size px.
// No external image assets — just plain LVGL shapes. Returns the icon's
// transparent root container; caller positions/aligns it as needed.
lv_obj_t *weather_icon_create(lv_obj_t *parent, int weather_code, lv_coord_t size);

#ifdef __cplusplus
}
#endif
