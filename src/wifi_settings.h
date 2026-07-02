#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds and shows the Wi-Fi settings screen: scans for nearby networks and
// lists them. Tapping the house icon returns to clock_scr.
void wifi_settings_show(lv_obj_t *clock_scr);

#ifdef __cplusplus
}
#endif
