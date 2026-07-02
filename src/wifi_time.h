#pragma once
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *ssid;
    const char *password;
} wifi_network_t;

// Creates the mutex/event-group used to serialize wifi_connect_any() and
// wifi_connect_one() against each other. Call once from app_main, before
// spawning any task that might call either of them (including a periodic
// reconnect task) — the actual Wi-Fi driver init is deferred to the first
// wifi_connect_any() call.
void wifi_time_init(void);

// Called before each connection attempt.  index is 0-based, total is count.
typedef void (*wifi_status_cb_t)(const char *ssid, size_t index, size_t total);

// Try each network in order; return ESP_OK on the first that connects.
// total_timeout_ms is shared across ALL attempts — the loop stops as soon as
// the budget is exhausted, even if not every network has been tried.
// cb is optional (pass NULL to skip).
esp_err_t wifi_connect_any(const wifi_network_t *networks, size_t count,
                            wifi_status_cb_t cb, uint32_t total_timeout_ms);

// Sync system time via NTP and set the local timezone.
// timezone: POSIX TZ string, e.g. "<-03>3" for Argentina UTC-3.
// Blocks up to 15 s waiting for a valid timestamp.
esp_err_t time_sync(const char *timezone, const char *ntp_server);

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    bool    secure;
} wifi_ap_info_t;

// Blocking scan for nearby Wi-Fi networks. Requires the Wi-Fi stack to already
// be initialized and started in STA mode (true after wifi_connect_any() has
// run, regardless of whether it returned ESP_OK).
// On success, *out_aps is malloc'd (caller must free()) and *out_count is set.
esp_err_t wifi_scan(wifi_ap_info_t **out_aps, uint16_t *out_count);

// Connect to a single network. Requires the Wi-Fi stack to already be
// initialized and started (true any time after wifi_connect_any() has run,
// regardless of its return value). Blocks up to timeout_ms. Must not be
// called from the LVGL task.
esp_err_t wifi_connect_one(const char *ssid, const char *password, uint32_t timeout_ms);

// Returns true if currently associated to an access point, and copies its
// SSID into out (out_size must be at least 33). Cheap, non-blocking, safe to
// call from any task including the LVGL task.
bool wifi_get_connected_ssid(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
