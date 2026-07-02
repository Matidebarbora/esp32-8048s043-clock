#pragma once
#include <stddef.h>
#include <stdbool.h>

#define WIFI_STORE_MAX_NETWORKS 8

typedef struct {
    char ssid[33];
    char password[65];
} wifi_saved_network_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initializes NVS (safe to call even if already initialized elsewhere) and
// loads previously-saved networks into RAM. Call once at startup, before
// wifi_store_get_all()/wifi_store_is_known().
void wifi_store_init(void);

// Returns a pointer to the in-RAM saved-network list (valid until the next
// wifi_store_save()/wifi_store_forget() call) and the number of entries.
size_t wifi_store_get_all(const wifi_saved_network_t **out);

// True if ssid has a saved password.
bool wifi_store_is_known(const char *ssid);

// Save (or update) credentials for ssid and persist to NVS immediately.
// If the store is full, the oldest saved network is evicted.
void wifi_store_save(const char *ssid, const char *password);

// Remove saved credentials for ssid, if any, and persist to NVS immediately.
void wifi_store_forget(const char *ssid);

#ifdef __cplusplus
}
#endif
