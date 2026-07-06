#include "notification_store.h"

#include <cctype>
#include <cstring>
#include "freertos/FreeRTOS.h"

static notification_t s_items[NOTIF_STORE_MAX];
static size_t         s_count = 0;
static portMUX_TYPE    s_mux  = portMUX_INITIALIZER_UNLOCKED;

static notification_store_changed_cb_t s_changed_cb = nullptr;

void notification_store_add(const char *app_name, const char *title, const char *message)
{
    taskENTER_CRITICAL(&s_mux);
    size_t n = s_count < NOTIF_STORE_MAX - 1 ? s_count : NOTIF_STORE_MAX - 1;
    memmove(&s_items[1], &s_items[0], n * sizeof(notification_t));
    notification_t *item = &s_items[0];
    strlcpy(item->app_name, app_name, sizeof(item->app_name));
    strlcpy(item->title, title, sizeof(item->title));
    strlcpy(item->message, message, sizeof(item->message));
    item->received_at = time(nullptr);
    if (s_count < NOTIF_STORE_MAX) s_count++;
    taskEXIT_CRITICAL(&s_mux);

    if (s_changed_cb) s_changed_cb();
}

void notification_store_clear(void)
{
    taskENTER_CRITICAL(&s_mux);
    s_count = 0;
    taskEXIT_CRITICAL(&s_mux);

    if (s_changed_cb) s_changed_cb();
}

size_t notification_store_get_all(notification_t *out, size_t max_count)
{
    taskENTER_CRITICAL(&s_mux);
    size_t n = s_count < max_count ? s_count : max_count;
    memcpy(out, s_items, n * sizeof(notification_t));
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

bool notification_store_get_latest(notification_t *out)
{
    taskENTER_CRITICAL(&s_mux);
    bool have = s_count > 0;
    if (have) *out = s_items[0];
    taskEXIT_CRITICAL(&s_mux);
    return have;
}

void notification_avatar_rgb(const char *app_name, uint8_t *r, uint8_t *g, uint8_t *b)
{
    static const uint8_t palette[][3] = {
        {29, 158, 117}, {55, 138, 221}, {212, 83, 126}, {240, 153, 123},
        {93, 202, 165}, {186, 117, 23}, {127, 119, 221}, {99, 153, 34},
    };
    uint32_t hash = 5381;
    for (const char *p = app_name; *p; p++) hash = hash * 33u + (uint8_t)*p;
    size_t idx = hash % (sizeof(palette) / sizeof(palette[0]));
    *r = palette[idx][0];
    *g = palette[idx][1];
    *b = palette[idx][2];
}

void notification_initials(const char *app_name, char *out, size_t out_size)
{
    if (out_size == 0) return;
    size_t oi = 0;
    bool   at_word_start = true;
    for (const char *p = app_name; *p && oi < out_size - 1 && oi < 2; p++) {
        if (*p == ' ') { at_word_start = true; continue; }
        if (at_word_start) {
            out[oi++]     = (char)toupper((unsigned char)*p);
            at_word_start = false;
        }
    }
    if (oi == 0 && app_name[0]) out[oi++] = (char)toupper((unsigned char)app_name[0]);
    out[oi] = '\0';
}

void notification_store_set_changed_cb(notification_store_changed_cb_t cb)
{
    s_changed_cb = cb;
}
