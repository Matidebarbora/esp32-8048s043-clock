#include "stocks_store.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "stocks_store";
static const char *NVS_NS = "stocks";

static stocks_store_entry_t s_symbols[STOCKS_STORE_MAX];
static size_t s_count = 0;

static stocks_store_changed_cb_t s_changed_cb = nullptr;

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (RW) failed");
        return;
    }
    nvs_set_u8(h, "count", (uint8_t)s_count);
    char key[16];
    for (size_t i = 0; i < s_count; i++) {
        snprintf(key, sizeof(key), "sym%u", (unsigned)i);
        nvs_set_str(h, key, s_symbols[i].symbol);
    }
    nvs_commit(h);
    nvs_close(h);
}

void stocks_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No pinned stocks yet");
        return;
    }

    uint8_t count = 0;
    nvs_get_u8(h, "count", &count);
    if (count > STOCKS_STORE_MAX) count = STOCKS_STORE_MAX;

    char key[16];
    size_t loaded = 0;
    for (uint8_t i = 0; i < count; i++) {
        snprintf(key, sizeof(key), "sym%u", (unsigned)i);
        size_t len = sizeof(s_symbols[loaded].symbol);
        if (nvs_get_str(h, key, s_symbols[loaded].symbol, &len) != ESP_OK) continue;
        loaded++;
    }
    s_count = loaded;
    nvs_close(h);

    ESP_LOGI(TAG, "Loaded %u pinned stock(s)", (unsigned)s_count);
}

size_t stocks_store_get_all(const stocks_store_entry_t **out)
{
    *out = s_symbols;
    return s_count;
}

bool stocks_store_is_pinned(const char *symbol)
{
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_symbols[i].symbol, symbol) == 0) return true;
    return false;
}

void stocks_store_pin(const char *symbol)
{
    if (stocks_store_is_pinned(symbol)) return;

    if (s_count >= STOCKS_STORE_MAX) {
        // Evict the oldest entry to make room.
        memmove(&s_symbols[0], &s_symbols[1],
                sizeof(stocks_store_entry_t) * (STOCKS_STORE_MAX - 1));
        s_count = STOCKS_STORE_MAX - 1;
    }

    strlcpy(s_symbols[s_count].symbol, symbol, sizeof(s_symbols[s_count].symbol));
    s_count++;
    persist();
    if (s_changed_cb) s_changed_cb();
}

void stocks_store_unpin(const char *symbol)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_symbols[i].symbol, symbol) == 0) {
            memmove(&s_symbols[i], &s_symbols[i + 1],
                    sizeof(stocks_store_entry_t) * (s_count - i - 1));
            s_count--;
            persist();
            if (s_changed_cb) s_changed_cb();
            return;
        }
    }
}

void stocks_store_set_changed_cb(stocks_store_changed_cb_t cb)
{
    s_changed_cb = cb;
}
