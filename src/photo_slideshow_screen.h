#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Full-screen photo slideshow (tap the time to open it). Reads *.bin images
// from SD_PHOTOS_DIR (see sd_card.h) — pre-converted to 800x480 raw LVGL
// binaries by tools/convert_photos.py, since this board has no PNG/JPEG
// decoder enabled. Auto-advances every PHOTO_INTERVAL_MS, shows the current
// time top-right, and returns to clock_scr on a tap anywhere.
void photo_slideshow_screen_show(lv_obj_t *clock_scr);

#ifdef __cplusplus
}
#endif
