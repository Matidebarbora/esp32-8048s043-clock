#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Settings sub-screen (gear icon > Notifications) — toggles for which
// notifications ancs_client.cpp stores/shows on the clock's notification
// card and history screen.
void notification_filter_screen_show(lv_obj_t *parent_scr);

#ifdef __cplusplus
}
#endif
