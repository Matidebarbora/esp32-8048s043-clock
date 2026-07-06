#include "notification_filter_settings.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "notif_filter";
static const char *NVS_NS = "notif_filter";

static bool s_hide_whatsapp = false;

void notification_filter_settings_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t v = 0;
        nvs_get_u8(h, "hide_whatsapp", &v);
        s_hide_whatsapp = v != 0;
        nvs_close(h);
    }
    ESP_LOGI(TAG, "hide_whatsapp: %s", s_hide_whatsapp ? "true" : "false");
}

bool notification_filter_get_hide_whatsapp(void)
{
    return s_hide_whatsapp;
}

void notification_filter_set_hide_whatsapp(bool hide)
{
    s_hide_whatsapp = hide;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "hide_whatsapp", hide ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "hide_whatsapp set to %s", hide ? "true" : "false");
}
