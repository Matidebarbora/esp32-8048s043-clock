#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "weather";

#define HTTP_BUF_SIZE 4096

struct HttpCtx {
    char  *buf;
    size_t len;
    size_t cap;
};

// "2026-07-02T07:15" -> "07:15". Falls back to "--:--" if the shape is off.
static void extract_hhmm(const cJSON *arr, int index, char *out, size_t out_size)
{
    strlcpy(out, "--:--", out_size);
    if (!arr) return;
    cJSON *item = cJSON_GetArrayItem(arr, index);
    if (!cJSON_IsString(item)) return;
    const char *t = strchr(item->valuestring, 'T');
    if (t && strlen(t + 1) >= 5) strlcpy(out, t + 1, out_size);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    auto *ctx = (HttpCtx *)evt->user_data;
    size_t remain = ctx->cap - ctx->len - 1;  // keep room for the null terminator
    size_t n = (size_t)evt->data_len < remain ? (size_t)evt->data_len : remain;
    if (n > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, n);
        ctx->len += n;
        ctx->buf[ctx->len] = '\0';
    }
    return ESP_OK;
}

esp_err_t weather_fetch(float latitude, float longitude, weather_data_t *out)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset"
             "&timezone=auto&forecast_days=%d",
             latitude, longitude, WEATHER_FORECAST_DAYS);

    char *buf = (char *)malloc(HTTP_BUF_SIZE);
    if (!buf) return ESP_ERR_NO_MEM;
    HttpCtx ctx = { buf, 0, HTTP_BUF_SIZE };

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.event_handler     = http_event_handler;
    cfg.user_data         = &ctx;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 10000;

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
    cJSON *current = cJSON_GetObjectItem(root, "current");
    cJSON *daily   = cJSON_GetObjectItem(root, "daily");
    if (current && daily) {
        cJSON *cur_temp = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *cur_code = cJSON_GetObjectItem(current, "weather_code");
        cJSON *time_arr   = cJSON_GetObjectItem(daily, "time");
        cJSON *max_arr    = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *min_arr    = cJSON_GetObjectItem(daily, "temperature_2m_min");
        cJSON *precip_arr  = cJSON_GetObjectItem(daily, "precipitation_probability_max");
        cJSON *code_arr    = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *sunrise_arr = cJSON_GetObjectItem(daily, "sunrise");
        cJSON *sunset_arr  = cJSON_GetObjectItem(daily, "sunset");

        if (cJSON_IsNumber(cur_temp) && cJSON_IsArray(time_arr) &&
            cJSON_IsArray(max_arr) && cJSON_IsArray(min_arr)) {
            out->current_c    = (float)cur_temp->valuedouble;
            out->weather_code = cJSON_IsNumber(cur_code) ? cur_code->valueint : -1;

            int n = cJSON_GetArraySize(time_arr);
            if (n > WEATHER_FORECAST_DAYS) n = WEATHER_FORECAST_DAYS;
            out->daily_count = 0;

            for (int i = 0; i < n; i++) {
                cJSON *t      = cJSON_GetArrayItem(time_arr, i);
                cJSON *max    = cJSON_GetArrayItem(max_arr, i);
                cJSON *min    = cJSON_GetArrayItem(min_arr, i);
                cJSON *precip = precip_arr ? cJSON_GetArrayItem(precip_arr, i) : nullptr;
                cJSON *code   = code_arr   ? cJSON_GetArrayItem(code_arr, i)   : nullptr;
                if (!cJSON_IsString(t) || !cJSON_IsNumber(max) || !cJSON_IsNumber(min)) continue;

                weather_daily_t *d = &out->daily[out->daily_count];
                strlcpy(d->date, t->valuestring, sizeof(d->date));
                d->max_c           = (float)max->valuedouble;
                d->min_c           = (float)min->valuedouble;
                d->precip_prob_max = cJSON_IsNumber(precip) ? precip->valueint : -1;
                d->weather_code    = cJSON_IsNumber(code) ? code->valueint : -1;
                out->daily_count++;
            }

            if (out->daily_count > 0) {
                out->min_c = out->daily[0].min_c;
                out->max_c = out->daily[0].max_c;
                extract_hhmm(sunrise_arr, 0, out->sunrise, sizeof(out->sunrise));
                extract_hhmm(sunset_arr,  0, out->sunset,  sizeof(out->sunset));
                parse_ret  = ESP_OK;
            } else {
                ESP_LOGW(TAG, "No usable daily entries");
            }
        } else {
            ESP_LOGW(TAG, "Unexpected JSON shape");
        }
    }

    cJSON_Delete(root);
    return parse_ret;
}

static weather_data_t s_last;
static bool           s_last_valid = false;

void weather_set_last(const weather_data_t *data)
{
    s_last       = *data;
    s_last_valid = true;
}

bool weather_get_last(weather_data_t *out)
{
    if (!s_last_valid) return false;
    *out = s_last;
    return true;
}

// WMO weather interpretation codes, per the Open-Meteo API docs.
const char *weather_code_description(int code)
{
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: return "Fog";
        case 48: return "Depositing rime fog";
        case 51: return "Light drizzle";
        case 53: return "Moderate drizzle";
        case 55: return "Dense drizzle";
        case 56: return "Light freezing drizzle";
        case 57: return "Dense freezing drizzle";
        case 61: return "Slight rain";
        case 63: return "Moderate rain";
        case 65: return "Heavy rain";
        case 66: return "Light freezing rain";
        case 67: return "Heavy freezing rain";
        case 71: return "Slight snow fall";
        case 73: return "Moderate snow fall";
        case 75: return "Heavy snow fall";
        case 77: return "Snow grains";
        case 80: return "Slight rain showers";
        case 81: return "Moderate rain showers";
        case 82: return "Violent rain showers";
        case 85: return "Slight snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96: return "Thunderstorm, slight hail";
        case 99: return "Thunderstorm, heavy hail";
        default: return "Unknown";
    }
}
