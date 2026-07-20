#include "background_settings.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "background_settings";
static const char *NVS_NS = "background";

const background_option_t BACKGROUND_OPTIONS[] = {
    { "Black",           8,  8, 10, false,  0,  0,  0 },
    { "Charcoal",       18, 18, 20, false,  0,  0,  0 },
    { "Navy",            8, 10, 22, false,  0,  0,  0 },
    { "Forest",          8, 18, 10, false,  0,  0,  0 },
    { "Plum",           18,  8, 20, false,  0,  0,  0 },
    { "Maroon",         22,  8, 10, false,  0,  0,  0 },
    { "Espresso",       20, 14,  8, false,  0,  0,  0 },
    { "Black to Brown",  8,  8, 10, true,  70, 42, 18 },
    { "Black to Navy",   8,  8, 10, true,  14, 22, 60 },
    { "Plum to Black",  40, 16, 48, true,   8,  8, 10 },
};
const int BACKGROUND_OPTION_COUNT = sizeof(BACKGROUND_OPTIONS) / sizeof(BACKGROUND_OPTIONS[0]);

static int s_index = 0;

static background_settings_changed_cb_t s_changed_cb = nullptr;

static int clamp_index(int idx)
{
    if (idx < 0 || idx >= BACKGROUND_OPTION_COUNT) return 0;
    return idx;
}

void background_settings_init(void)
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
        if (nvs_get_u8(h, "index", &v) == ESP_OK) s_index = clamp_index(v);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Background: %s", BACKGROUND_OPTIONS[s_index].name);
}

int background_settings_get_index(void)
{
    return s_index;
}

void background_settings_set_index(int index)
{
    s_index = clamp_index(index);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "index", (uint8_t)s_index);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Background set to %s", BACKGROUND_OPTIONS[s_index].name);

    if (s_changed_cb) s_changed_cb();
}

void background_settings_set_changed_cb(background_settings_changed_cb_t cb)
{
    s_changed_cb = cb;
}
