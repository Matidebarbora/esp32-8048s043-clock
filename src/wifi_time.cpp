#include "wifi_time.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_group;
#define CONNECTED_BIT BIT0
#define FAIL_BIT      BIT1

// One-time init (nvs/netif/wifi driver) happens only the first time any
// connect function runs. Retries later just reuse the already-started
// driver — calling esp_wifi_init()/esp_netif_create_default_wifi_sta() a
// second time aborts the app (ESP_ERROR_CHECK on an already-initialized
// state).
static bool s_wifi_initialized = false;

// Serializes esp_wifi_set_config()/esp_wifi_connect() across callers —
// wifi_connect_any() (startup, periodic reconnect) and wifi_connect_one()
// (manual connect from the Wi-Fi settings screen) run on different tasks and
// must not touch the driver at the same time.
static SemaphoreHandle_t s_wifi_op_mutex;

// Returns elapsed milliseconds since a reference tick
static inline uint32_t elapsed_ms(TickType_t t_start)
{
    return (uint32_t)((xTaskGetTickCount() - t_start) * portTICK_PERIOD_MS);
}

static void on_event(void *arg, esp_event_base_t base,
                     int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_group, FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(s_wifi_group, CONNECTED_BIT);
    }
}

void wifi_time_init(void)
{
    s_wifi_group    = xEventGroupCreate();
    s_wifi_op_mutex = xSemaphoreCreateMutex();
}

// Caller must already hold s_wifi_op_mutex.
static void wifi_stack_init_once()
{
    if (s_wifi_initialized) return;

    ESP_LOGI(TAG, "Initializing WiFi stack...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, reflashing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS ready");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_initialized = true;
}

esp_err_t wifi_connect_any(const wifi_network_t *networks, size_t count,
                            wifi_status_cb_t cb, uint32_t total_timeout_ms)
{
    xSemaphoreTake(s_wifi_op_mutex, portMAX_DELAY);
    wifi_stack_init_once();

    ESP_LOGI(TAG, "WiFi started — budget: %lu ms, networks: %zu", (unsigned long)total_timeout_ms, count);

    TickType_t t_start    = xTaskGetTickCount();
    TickType_t t_deadline = t_start + pdMS_TO_TICKS(total_timeout_ms);
    esp_err_t  result     = ESP_FAIL;

    for (size_t i = 0; i < count; i++) {
        // How much budget is left?
        TickType_t t_now       = xTaskGetTickCount();
        TickType_t t_remaining = t_deadline - t_now;

        if ((int32_t)t_remaining <= 0) {
            ESP_LOGW(TAG, "[%lu ms] Budget exhausted before trying \"%s\"",
                     (unsigned long)elapsed_ms(t_start), networks[i].ssid);
            break;
        }

        ESP_LOGI(TAG, "[%lu ms] Trying \"%s\" (%zu/%zu), %lu ms remaining...",
                 (unsigned long)elapsed_ms(t_start),
                 networks[i].ssid, i + 1, count,
                 (unsigned long)(t_remaining * portTICK_PERIOD_MS));

        if (cb) cb(networks[i].ssid, i, count);

        xEventGroupClearBits(s_wifi_group, CONNECTED_BIT | FAIL_BIT);

        wifi_config_t wifi_cfg = {};
        strlcpy((char *)wifi_cfg.sta.ssid,     networks[i].ssid,     sizeof(wifi_cfg.sta.ssid));
        strlcpy((char *)wifi_cfg.sta.password, networks[i].password, sizeof(wifi_cfg.sta.password));

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_connect());

        EventBits_t bits = xEventGroupWaitBits(s_wifi_group,
                                               CONNECTED_BIT | FAIL_BIT,
                                               pdFALSE, pdFALSE,
                                               t_remaining);

        if (bits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "[%lu ms] Connected to \"%s\"",
                     (unsigned long)elapsed_ms(t_start), networks[i].ssid);
            result = ESP_OK;
            break;
        }

        if (bits == 0) {
            // xEventGroupWaitBits timed out — budget is gone
            ESP_LOGW(TAG, "[%lu ms] Total timeout reached on \"%s\"",
                     (unsigned long)elapsed_ms(t_start), networks[i].ssid);
            break;
        }

        ESP_LOGW(TAG, "[%lu ms] \"%s\" unreachable, trying next...",
                 (unsigned long)elapsed_ms(t_start), networks[i].ssid);
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (result != ESP_OK)
        ESP_LOGE(TAG, "All networks failed after %lu ms", (unsigned long)elapsed_ms(t_start));

    xSemaphoreGive(s_wifi_op_mutex);
    return result;
}

esp_err_t wifi_scan(wifi_ap_info_t **out_aps, uint16_t *out_count)
{
    *out_aps   = nullptr;
    *out_count = 0;

    // esp_wifi_scan_start() fails immediately with ESP_ERR_WIFI_STATE if a
    // connect attempt is already in progress on the driver — which happens
    // routinely, since wifi_reconnect_task calls wifi_connect_any() every
    // 10 s whenever Wi-Fi is down (the exact moment a user is likely to open
    // this screen). Share s_wifi_op_mutex with connect so the scan simply
    // waits its turn instead of racing and failing.
    xSemaphoreTake(s_wifi_op_mutex, portMAX_DELAY);

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;

    ESP_LOGI(TAG, "Scanning for networks...");
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);  // blocking
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_wifi_op_mutex);
        return ret;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        ESP_LOGI(TAG, "Scan found no networks");
        xSemaphoreGive(s_wifi_op_mutex);
        return ESP_OK;
    }

    auto *records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!records) {
        xSemaphoreGive(s_wifi_op_mutex);
        return ESP_ERR_NO_MEM;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, records);
    xSemaphoreGive(s_wifi_op_mutex);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan get records failed: %s", esp_err_to_name(ret));
        free(records);
        return ret;
    }

    auto *aps = (wifi_ap_info_t *)malloc(sizeof(wifi_ap_info_t) * ap_count);
    if (!aps) {
        free(records);
        return ESP_ERR_NO_MEM;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        strlcpy(aps[i].ssid, (const char *)records[i].ssid, sizeof(aps[i].ssid));
        aps[i].rssi   = records[i].rssi;
        aps[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
    }
    free(records);

    ESP_LOGI(TAG, "Scan found %u networks", ap_count);
    *out_aps   = aps;
    *out_count = ap_count;
    return ESP_OK;
}

esp_err_t wifi_connect_one(const char *ssid, const char *password, uint32_t timeout_ms)
{
    xSemaphoreTake(s_wifi_op_mutex, portMAX_DELAY);
    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);

    xEventGroupClearBits(s_wifi_group, CONNECTED_BIT | FAIL_BIT);

    wifi_config_t wifi_cfg = {};
    strlcpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret == ESP_OK) {
        esp_wifi_disconnect();  // drop any existing connection first
        ret = esp_wifi_connect();
    }

    if (ret == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_group, CONNECTED_BIT | FAIL_BIT,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
        if (bits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to \"%s\"", ssid);
            ret = ESP_OK;
        } else {
            ESP_LOGW(TAG, "Failed to connect to \"%s\"", ssid);
            ret = ESP_FAIL;
        }
    }

    xSemaphoreGive(s_wifi_op_mutex);
    return ret;
}

bool wifi_get_connected_ssid(char *out, size_t out_size)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return false;
    strlcpy(out, (const char *)info.ssid, out_size);
    return true;
}

esp_err_t time_sync(const char *timezone, const char *ntp_server)
{
    ESP_LOGI(TAG, "Setting timezone: %s", timezone);
    setenv("TZ", timezone, 1);
    tzset();

    ESP_LOGI(TAG, "Contacting NTP server: %s", ntp_server);
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
    esp_netif_sntp_init(&cfg);

    TickType_t t0  = xTaskGetTickCount();
    esp_err_t  ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();

    if (ret == ESP_OK) {
        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);
        ESP_LOGI(TAG, "NTP synced in %lu ms: %04d-%02d-%02d %02d:%02d:%02d",
                 (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS),
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        ESP_LOGE(TAG, "NTP sync timed out after %lu ms",
                 (unsigned long)((xTaskGetTickCount() - t0) * portTICK_PERIOD_MS));
    }
    return ret;
}
