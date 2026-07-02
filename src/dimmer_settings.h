#pragma once
#include <stdbool.h>

typedef enum { DIMMER_OFF = 0, DIMMER_ON = 1, DIMMER_AUTO = 2 } dimmer_mode_t;

typedef struct {
    dimmer_mode_t mode;
    int dim_start_hour, dim_start_min;  // AUTO: time of day to switch TO dim
    int dim_end_hour,   dim_end_min;    // AUTO: time of day to switch back TO bright
} dimmer_settings_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initializes NVS (safe even if already initialized elsewhere) and loads the
// persisted dimmer settings. Call once at startup.
void dimmer_settings_init(void);

dimmer_settings_t dimmer_settings_get(void);

// Persist immediately and notify the registered change callback (if any).
void dimmer_settings_set_mode(dimmer_mode_t mode);
void dimmer_settings_set_schedule(int start_h, int start_m, int end_h, int end_m);

// Called after any change (mode or schedule) is persisted, so the app can
// re-apply the brightness policy immediately instead of waiting for the next
// periodic check. Only one callback is supported (main.cpp's dimmer task).
typedef void (*dimmer_settings_changed_cb_t)(void);
void dimmer_settings_set_changed_cb(dimmer_settings_changed_cb_t cb);

#ifdef __cplusplus
}
#endif
