#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// DST_AUTO (default): time zone follows the IP-geolocated UTC offset
// (app_settings_apply_utc_offset()); WINTER/SUMMER force the old fixed Chile
// offsets as a manual override.
typedef enum { DST_WINTER = 0, DST_SUMMER = 1, DST_AUTO = 2 } dst_mode_t;

// Initializes NVS (safe even if already initialized elsewhere), loads the
// persisted DST mode, and applies it (setenv("TZ", ...) + tzset()) right
// away. Call once at startup, before time_sync() or any manual time entry —
// both rely on TZ already being set.
void app_settings_init(void);

dst_mode_t app_settings_get_dst_mode(void);

// Change DST mode: persists to NVS and immediately re-applies TZ/tzset() so
// every subsequent time read (clock display, manual time entry) reflects it.
void app_settings_set_dst_mode(dst_mode_t mode);

// Feed the current UTC offset (seconds, including DST) from IP geolocation.
// Cached in NVS (so the next boot has a sensible TZ before the network is
// up) and, in DST_AUTO mode, applied immediately. Not LVGL-thread-safe by
// itself: callers that race with the clock tick should hold lvgl_acquire().
void app_settings_apply_utc_offset(int offset_sec);

// POSIX TZ string for the currently active DST mode.
const char *app_settings_get_tz(void);

#ifdef __cplusplus
}
#endif
