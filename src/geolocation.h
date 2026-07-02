#pragma once
#include "esp_err.h"

typedef struct {
    char  city[32];
    float lat;
    float lon;
} geo_location_t;

#ifdef __cplusplus
extern "C" {
#endif

// Blocking IP-based geolocation lookup (ip-api.com's free tier — no API key,
// plain HTTP). Must not be called from the LVGL task. Falls back to the
// caller on failure; caller should keep using its own default coordinates.
esp_err_t geolocation_fetch(geo_location_t *out);

#ifdef __cplusplus
}
#endif
