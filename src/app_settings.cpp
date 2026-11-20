#include "app_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "app_settings";
static const char *NVS_NS = "app_settings";

// Chile: standard (winter) time is UTC-4; daylight saving (summer) time is
// UTC-3, one hour *ahead* of winter — not behind it. TZ_SUMMER was
// previously UTC-5 (two hours off, in the wrong direction), making summer
// clocks read 2h behind what they should.
#define TZ_WINTER "<-04>4"
#define TZ_SUMMER "<-03>3"

// Auto mode before any geolocation has ever succeeded: UTC-3, matching
// config.h's FALLBACK_LATITUDE/LONGITUDE (Buenos Aires).
#define AUTO_FALLBACK_OFFSET_SEC (-3 * 3600)

static dst_mode_t s_dst_mode   = DST_AUTO;
static int        s_utc_offset = AUTO_FALLBACK_OFFSET_SEC;
static char       s_auto_tz[20];

// Builds a POSIX TZ string for a fixed UTC offset, e.g. -10800 -> "<-03>3",
// +19800 -> "<+0530>-5:30". (POSIX offsets have the opposite sign.)
static void build_fixed_tz(char *out, size_t n, int offset_sec)
{
    int ao = offset_sec < 0 ? -offset_sec : offset_sec;
    int h = ao / 3600, m = (ao % 3600) / 60;
    char name[12];
    if (m) snprintf(name, sizeof(name), "<%c%02d%02d>", offset_sec < 0 ? '-' : '+', h, m);
    else   snprintf(name, sizeof(name), "<%c%02d>",     offset_sec < 0 ? '-' : '+', h);
    if (m) snprintf(out, n, "%s%s%d:%02d", name, offset_sec > 0 ? "-" : "", h, m);
    else   snprintf(out, n, "%s%s%d",      name, offset_sec > 0 ? "-" : "", h);
}

static void apply_tz(void)
{
    setenv("TZ", app_settings_get_tz(), 1);
    tzset();
}

void app_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t mode = DST_AUTO;  // nothing saved yet -> automatic
        nvs_get_u8(h, "dst_mode", &mode);
        s_dst_mode = (mode == DST_SUMMER) ? DST_SUMMER : (mode == DST_WINTER) ? DST_WINTER : DST_AUTO;
        int32_t off;
        if (nvs_get_i32(h, "utc_off", &off) == ESP_OK) s_utc_offset = off;
        nvs_close(h);
    }

    apply_tz();
    ESP_LOGI(TAG, "DST mode: %s (%s)",
             s_dst_mode == DST_SUMMER ? "summer" : s_dst_mode == DST_WINTER ? "winter" : "auto",
             app_settings_get_tz());
}

void app_settings_apply_utc_offset(int offset_sec)
{
    if (offset_sec == s_utc_offset) return;  // nothing to do (also avoids a needless tzset)
    s_utc_offset = offset_sec;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "utc_off", offset_sec);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "IP-derived UTC offset now %d s", offset_sec);
    if (s_dst_mode == DST_AUTO) apply_tz();
}

dst_mode_t app_settings_get_dst_mode(void)
{
    return s_dst_mode;
}

void app_settings_set_dst_mode(dst_mode_t mode)
{
    s_dst_mode = mode;
    apply_tz();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "dst_mode", (uint8_t)mode);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "DST mode set to %s (%s)",
             mode == DST_SUMMER ? "summer" : mode == DST_WINTER ? "winter" : "auto",
             app_settings_get_tz());
}

const char *app_settings_get_tz(void)
{
    if (s_dst_mode == DST_SUMMER) return TZ_SUMMER;
    if (s_dst_mode == DST_WINTER) return TZ_WINTER;
    build_fixed_tz(s_auto_tz, sizeof(s_auto_tz), s_utc_offset);
    return s_auto_tz;
}
