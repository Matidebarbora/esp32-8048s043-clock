#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define WEATHER_FORECAST_DAYS 5

typedef struct {
    char  date[11];  // "YYYY-MM-DD"
    float min_c;
    float max_c;
    int   precip_prob_max;  // % chance of precipitation that day, 0-100
    int   weather_code;     // WMO code for that day (see weather_code_description())
} weather_daily_t;

typedef struct {
    float min_c;         // today's min (== daily[0].min_c)
    float current_c;
    float max_c;         // today's max (== daily[0].max_c)
    int   weather_code;  // WMO code (see weather_code_description())
    weather_daily_t daily[WEATHER_FORECAST_DAYS];
    int   daily_count;
} weather_data_t;

#ifdef __cplusplus
extern "C" {
#endif

// Blocking HTTPS GET + JSON parse against Open-Meteo (no API key needed).
// Must not be called from the LVGL task — a TLS handshake + request can take
// a few seconds.
esp_err_t weather_fetch(float latitude, float longitude, weather_data_t *out);

// Maps a WMO weather interpretation code (as returned by Open-Meteo) to a
// short human-readable description. Never returns NULL.
const char *weather_code_description(int code);

// Caches the most recently fetched data so other screens can read it without
// triggering a new network request. Caller (main.cpp's weather_task) must
// call this from inside the same lvgl_acquire()/lvgl_release() section it
// uses to update the clock screen's labels — weather_get_last() relies on
// that same lock being held (it's called from LVGL event handlers, which
// already hold it).
void weather_set_last(const weather_data_t *data);

// Returns the cached data from the last successful weather_fetch(). False if
// nothing has been fetched yet.
bool weather_get_last(weather_data_t *out);

#ifdef __cplusplus
}
#endif
