#include "app_settings.h"

#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "app_settings";
static const char *NVS_NS = "app_settings";

// Winter is 1 hour behind the previous UTC-3 default (UTC-4); summer is
// 2 hours behind that same reference (UTC-5).
#define TZ_WINTER "<-04>4"
#define TZ_SUMMER "<-05>5"

static dst_mode_t s_dst_mode = DST_WINTER;

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
        uint8_t mode = DST_WINTER;
        nvs_get_u8(h, "dst_mode", &mode);
        s_dst_mode = (mode == DST_SUMMER) ? DST_SUMMER : DST_WINTER;
        nvs_close(h);
    }

    apply_tz();
    ESP_LOGI(TAG, "DST mode: %s (%s)",
             s_dst_mode == DST_SUMMER ? "summer" : "winter", app_settings_get_tz());
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
             mode == DST_SUMMER ? "summer" : "winter", app_settings_get_tz());
}

const char *app_settings_get_tz(void)
{
    return s_dst_mode == DST_SUMMER ? TZ_SUMMER : TZ_WINTER;
}
