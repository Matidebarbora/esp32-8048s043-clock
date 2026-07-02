#include "wifi_store.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG    = "wifi_store";
static const char *NVS_NS = "wifi_store";

static wifi_saved_network_t s_networks[WIFI_STORE_MAX_NETWORKS];
static size_t s_count = 0;

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
        snprintf(key, sizeof(key), "ssid%u", (unsigned)i);
        nvs_set_str(h, key, s_networks[i].ssid);
        snprintf(key, sizeof(key), "pass%u", (unsigned)i);
        nvs_set_str(h, key, s_networks[i].password);
    }
    nvs_commit(h);
    nvs_close(h);
}

void wifi_store_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No saved networks yet");
        return;
    }

    uint8_t count = 0;
    nvs_get_u8(h, "count", &count);
    if (count > WIFI_STORE_MAX_NETWORKS) count = WIFI_STORE_MAX_NETWORKS;

    char key[16];
    size_t loaded = 0;
    for (uint8_t i = 0; i < count; i++) {
        size_t len;

        snprintf(key, sizeof(key), "ssid%u", (unsigned)i);
        len = sizeof(s_networks[loaded].ssid);
        if (nvs_get_str(h, key, s_networks[loaded].ssid, &len) != ESP_OK) continue;

        snprintf(key, sizeof(key), "pass%u", (unsigned)i);
        len = sizeof(s_networks[loaded].password);
        if (nvs_get_str(h, key, s_networks[loaded].password, &len) != ESP_OK) continue;

        loaded++;
    }
    s_count = loaded;
    nvs_close(h);

    ESP_LOGI(TAG, "Loaded %u saved network(s)", (unsigned)s_count);
}

size_t wifi_store_get_all(const wifi_saved_network_t **out)
{
    *out = s_networks;
    return s_count;
}

bool wifi_store_is_known(const char *ssid)
{
    for (size_t i = 0; i < s_count; i++)
        if (strcmp(s_networks[i].ssid, ssid) == 0) return true;
    return false;
}

void wifi_store_save(const char *ssid, const char *password)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_networks[i].ssid, ssid) == 0) {
            strlcpy(s_networks[i].password, password, sizeof(s_networks[i].password));
            persist();
            return;
        }
    }

    if (s_count >= WIFI_STORE_MAX_NETWORKS) {
        // Evict the oldest entry to make room.
        memmove(&s_networks[0], &s_networks[1],
                sizeof(wifi_saved_network_t) * (WIFI_STORE_MAX_NETWORKS - 1));
        s_count = WIFI_STORE_MAX_NETWORKS - 1;
    }

    strlcpy(s_networks[s_count].ssid, ssid, sizeof(s_networks[s_count].ssid));
    strlcpy(s_networks[s_count].password, password, sizeof(s_networks[s_count].password));
    s_count++;
    persist();
}

void wifi_store_forget(const char *ssid)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_networks[i].ssid, ssid) == 0) {
            memmove(&s_networks[i], &s_networks[i + 1],
                    sizeof(wifi_saved_network_t) * (s_count - i - 1));
            s_count--;
            persist();
            return;
        }
    }
}
