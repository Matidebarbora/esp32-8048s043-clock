#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Brand icons for known notification-source apps — see notification_icon.cpp.
// Twemoji-style pipeline reused from weather_icons_data.c, but sourced from
// Simple Icons (github.com/simple-icons/simple-icons, CC0) instead, since
// these are app/brand logos rather than emoji.
extern const lv_img_dsc_t img_app_whatsapp_36;
extern const lv_img_dsc_t img_app_gmail_36;

#ifdef __cplusplus
}
#endif
