#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Settings sub-screen: search Yahoo Finance for a symbol/company name and
// tap a result to pin it (up to STOCKS_STORE_MAX) to the clock screen's
// stock card. parent_scr is the Settings menu screen to return to.
void stocks_screen_show(lv_obj_t *parent_scr);

#ifdef __cplusplus
}
#endif
