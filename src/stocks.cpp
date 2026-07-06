#include "stocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "stocks";

#define HTTP_BUF_SIZE 4096

// Yahoo Finance blocks requests with no (or an obviously non-browser)
// User-Agent header — both endpoints below need this to get a real response
// instead of a 999/429.
#define YAHOO_USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
    "(KHTML, like Gecko) Chrome/120.0 Safari/537.36"

struct HttpCtx {
    char  *buf;
    size_t len;
    size_t cap;
};

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

// Percent-encodes anything that isn't alphanumeric (spaces become '+', the
// common convention for query strings) — good enough for the ticker/company
// name searches this feature deals with.
static void url_encode_query(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            out[j++] = (char)c;
        } else if (c == ' ') {
            out[j++] = '+';
        } else {
            j += snprintf(out + j, out_size - j, "%%%02X", c);
        }
    }
    out[j] = '\0';
}

// Shared by both endpoints: builds the URL, performs a blocking HTTPS GET
// into a malloc'd buffer, and parses it as JSON. Caller must cJSON_Delete()
// the result. Returns nullptr on any failure (already logged).
static cJSON *http_get_json(const char *url)
{
    char *buf = (char *)malloc(HTTP_BUF_SIZE);
    if (!buf) return nullptr;
    HttpCtx ctx = { buf, 0, HTTP_BUF_SIZE };

    esp_http_client_config_t cfg = {};
    cfg.url               = url;
    cfg.event_handler     = http_event_handler;
    cfg.user_data         = &ctx;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms        = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "User-Agent", YAHOO_USER_AGENT);
    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "HTTP request failed: ret=%s status=%d, body: %.200s",
                 esp_err_to_name(ret), status, buf);
        free(buf);
        return nullptr;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) ESP_LOGW(TAG, "JSON parse failed, body: %.200s", buf);
    free(buf);
    return root;
}

esp_err_t stock_search(const char *query, stock_search_result_t *out,
                        size_t max_results, size_t *out_count)
{
    *out_count = 0;

    char encoded[128];
    url_encode_query(query, encoded, sizeof(encoded));

    char url[220];
    snprintf(url, sizeof(url),
             "https://query1.finance.yahoo.com/v1/finance/search?q=%s"
             "&quotesCount=%u&newsCount=0&listsCount=0",
             encoded, (unsigned)max_results);

    cJSON *root = http_get_json(url);
    if (!root) return ESP_FAIL;

    cJSON *quotes = cJSON_GetObjectItem(root, "quotes");
    if (!cJSON_IsArray(quotes)) {
        char *dump = cJSON_PrintUnformatted(root);
        ESP_LOGW(TAG, "No \"quotes\" array in response: %.200s", dump ? dump : "(print failed)");
        cJSON_free(dump);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, quotes) {
        if (count >= max_results) break;

        cJSON *symbol   = cJSON_GetObjectItem(item, "symbol");
        cJSON *type     = cJSON_GetObjectItem(item, "quoteType");
        if (!cJSON_IsString(symbol)) continue;
        if (cJSON_IsString(type) && strcmp(type->valuestring, "EQUITY") != 0) continue;

        cJSON *shortname = cJSON_GetObjectItem(item, "shortname");
        cJSON *longname  = cJSON_GetObjectItem(item, "longname");
        const char *name = cJSON_IsString(shortname) ? shortname->valuestring
                          : cJSON_IsString(longname)  ? longname->valuestring
                          : "";

        strlcpy(out[count].symbol, symbol->valuestring, sizeof(out[count].symbol));
        strlcpy(out[count].name,   name,                sizeof(out[count].name));
        count++;
    }

    cJSON_Delete(root);
    *out_count = count;
    return ESP_OK;
}

esp_err_t stock_quote_fetch(const char *symbol, stock_quote_t *out)
{
    memset(out, 0, sizeof(*out));
    strlcpy(out->symbol, symbol, sizeof(out->symbol));

    char url[160];
    snprintf(url, sizeof(url),
             "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=1d&interval=1d",
             symbol);

    cJSON *root = http_get_json(url);
    if (!root) return ESP_FAIL;

    esp_err_t parse_ret = ESP_FAIL;
    cJSON *chart  = cJSON_GetObjectItem(root, "chart");
    cJSON *result = chart ? cJSON_GetObjectItem(chart, "result") : nullptr;
    cJSON *first  = cJSON_IsArray(result) ? cJSON_GetArrayItem(result, 0) : nullptr;
    cJSON *meta   = first ? cJSON_GetObjectItem(first, "meta") : nullptr;

    if (meta) {
        cJSON *price = cJSON_GetObjectItem(meta, "regularMarketPrice");
        cJSON *prev  = cJSON_GetObjectItem(meta, "previousClose");
        if (!cJSON_IsNumber(prev)) prev = cJSON_GetObjectItem(meta, "chartPreviousClose");

        if (cJSON_IsNumber(price) && cJSON_IsNumber(prev)) {
            out->price = (float)price->valuedouble;
            out->up    = price->valuedouble >= prev->valuedouble;
            out->valid = true;
            parse_ret  = ESP_OK;
        } else {
            ESP_LOGW(TAG, "%s: unexpected quote JSON shape", symbol);
        }
    } else {
        ESP_LOGW(TAG, "%s: no chart data (bad symbol?)", symbol);
    }

    cJSON_Delete(root);
    return parse_ret;
}
