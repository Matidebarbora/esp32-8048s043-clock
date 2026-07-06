#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define NOTIF_STORE_MAX 20

typedef struct {
    char   app_name[32];  // prettified, e.g. "WhatsApp"
    char   title[40];
    char   message[128];
    time_t received_at;
} notification_t;

#ifdef __cplusplus
extern "C" {
#endif

// Adds a new notification at the front (most recent first), dropping the
// oldest once full. Thread-safe — callable from the ANCS BLE callback (a
// different task than the UI).
void notification_store_add(const char *app_name, const char *title, const char *message);

// Removes everything ("remove all" on the notification screen).
void notification_store_clear(void);

// Copies up to max_count entries (most recent first) into out. Returns the
// number actually copied.
size_t notification_store_get_all(notification_t *out, size_t max_count);

// True if there's at least one notification; copies the latest into *out.
bool notification_store_get_latest(notification_t *out);

// Deterministic RGB color for an app name, so the same app always gets the
// same avatar color on both the persistent card and the list screen.
void notification_avatar_rgb(const char *app_name, uint8_t *r, uint8_t *g, uint8_t *b);

// Up to 2 uppercase initials derived from app_name, null-terminated.
void notification_initials(const char *app_name, char *out, size_t out_size);

typedef void (*notification_store_changed_cb_t)(void);
// Called (from whichever task called add/clear) after the store changes —
// the UI uses this to refresh without polling.
void notification_store_set_changed_cb(notification_store_changed_cb_t cb);

#ifdef __cplusplus
}
#endif
