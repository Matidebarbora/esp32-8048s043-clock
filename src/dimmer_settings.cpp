#include "dimmer_settings.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "dimmer_settings";
static const char *NVS_NS = "dimmer";

// Default schedule: dim at 22:00, back to bright at 07:00.
static dimmer_settings_t s_settings = { DIMMER_OFF, 22, 0, 7, 0 };

static dimmer_settings_changed_cb_t s_changed_cb = nullptr;

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (RW) failed");
        return;
    }
    nvs_set_u8(h, "mode",  (uint8_t)s_settings.mode);
    nvs_set_u8(h, "sh",    (uint8_t)s_settings.dim_start_hour);
    nvs_set_u8(h, "sm",    (uint8_t)s_settings.dim_start_min);
    nvs_set_u8(h, "eh",    (uint8_t)s_settings.dim_end_hour);
    nvs_set_u8(h, "em",    (uint8_t)s_settings.dim_end_min);
    nvs_commit(h);
    nvs_close(h);
}

void dimmer_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved dimmer settings yet, using defaults");
        return;
    }

    uint8_t v;
    if (nvs_get_u8(h, "mode", &v) == ESP_OK) s_settings.mode           = (dimmer_mode_t)v;
    if (nvs_get_u8(h, "sh",   &v) == ESP_OK) s_settings.dim_start_hour = v;
    if (nvs_get_u8(h, "sm",   &v) == ESP_OK) s_settings.dim_start_min  = v;
    if (nvs_get_u8(h, "eh",   &v) == ESP_OK) s_settings.dim_end_hour   = v;
    if (nvs_get_u8(h, "em",   &v) == ESP_OK) s_settings.dim_end_min    = v;
    nvs_close(h);

    ESP_LOGI(TAG, "Loaded: mode=%d dim %02d:%02d -> bright %02d:%02d",
             s_settings.mode, s_settings.dim_start_hour, s_settings.dim_start_min,
             s_settings.dim_end_hour, s_settings.dim_end_min);
}

dimmer_settings_t dimmer_settings_get(void)
{
    return s_settings;
}

void dimmer_settings_set_mode(dimmer_mode_t mode)
{
    s_settings.mode = mode;
    persist();
    if (s_changed_cb) s_changed_cb();
}

void dimmer_settings_set_schedule(int start_h, int start_m, int end_h, int end_m)
{
    s_settings.dim_start_hour = start_h;
    s_settings.dim_start_min  = start_m;
    s_settings.dim_end_hour   = end_h;
    s_settings.dim_end_min    = end_m;
    persist();
    if (s_changed_cb) s_changed_cb();
}

void dimmer_settings_set_changed_cb(dimmer_settings_changed_cb_t cb)
{
    s_changed_cb = cb;
}
