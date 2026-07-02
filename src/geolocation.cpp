#include "geolocation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "geolocation";

#define HTTP_BUF_SIZE 1024

struct HttpCtx {
    char  *buf;
    size_t len;
    size_t cap;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    auto *ctx = (HttpCtx *)evt->user_data;
    size_t remain = ctx->cap - ctx->len - 1;
    size_t n = (size_t)evt->data_len < remain ? (size_t)evt->data_len : remain;
    if (n > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, n);
        ctx->len += n;
        ctx->buf[ctx->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t geolocation_fetch(geo_location_t *out)
{
    char *buf = (char *)malloc(HTTP_BUF_SIZE);
    if (!buf) return ESP_ERR_NO_MEM;
    HttpCtx ctx = { buf, 0, HTTP_BUF_SIZE };

    // Free tier is plain HTTP only (HTTPS needs a paid plan) — fine here,
    // there's nothing sensitive in an IP-geolocation lookup.
    esp_http_client_config_t cfg = {};
    cfg.url           = "http://ip-api.com/json/?fields=status,message,city,lat,lon";
    cfg.event_handler = http_event_handler;
    cfg.user_data     = &ctx;
    cfg.timeout_ms    = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "HTTP request failed: ret=%s status=%d", esp_err_to_name(ret), status);
        free(buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    esp_err_t parse_ret = ESP_FAIL;
    cJSON *st   = cJSON_GetObjectItem(root, "status");
    cJSON *city = cJSON_GetObjectItem(root, "city");
    cJSON *lat  = cJSON_GetObjectItem(root, "lat");
    cJSON *lon  = cJSON_GetObjectItem(root, "lon");

    if (cJSON_IsString(st) && strcmp(st->valuestring, "success") == 0 &&
        cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        strlcpy(out->city, cJSON_IsString(city) ? city->valuestring : "", sizeof(out->city));
        out->lat = (float)lat->valuedouble;
        out->lon = (float)lon->valuedouble;
        parse_ret = ESP_OK;
        ESP_LOGI(TAG, "Located: %s (%.4f, %.4f)", out->city, out->lat, out->lon);
    } else {
        ESP_LOGW(TAG, "Geolocation lookup unsuccessful");
    }

    cJSON_Delete(root);
    return parse_ret;
}
