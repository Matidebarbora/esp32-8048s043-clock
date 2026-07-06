#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds a size x size notification avatar for app_name: a real brand icon
// for known apps (WhatsApp, Gmail, ...), or a colored circle with initials
// as a fallback for anything else. Brand icon assets are generated at 36px
// (see app_icons_data.c) and zoomed to fit other sizes. Returns the root
// object; caller positions/aligns it as needed.
lv_obj_t *notification_icon_create(lv_obj_t *parent, const char *app_name, lv_coord_t size);

#ifdef __cplusplus
}
#endif
