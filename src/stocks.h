#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#define STOCK_SYMBOL_LEN         16
#define STOCK_NAME_LEN           64
#define STOCK_SEARCH_MAX_RESULTS 8

typedef struct {
    char symbol[STOCK_SYMBOL_LEN];
    char name[STOCK_NAME_LEN];
} stock_search_result_t;

typedef struct {
    char  symbol[STOCK_SYMBOL_LEN];
    float price;
    bool  up;     // true if price >= previous close
    bool  valid;  // false if the fetch for this symbol failed
} stock_quote_t;

#ifdef __cplusplus
extern "C" {
#endif

// Blocking HTTPS GET + JSON parse against Yahoo Finance's unofficial search
// endpoint (no API key needed/available). Fills up to max_results entries in
// out and sets *out_count. Must not be called from the LVGL task.
esp_err_t stock_search(const char *query, stock_search_result_t *out,
                        size_t max_results, size_t *out_count);

// Blocking HTTPS GET + JSON parse against Yahoo Finance's unofficial chart
// endpoint for a single symbol's latest price. Must not be called from the
// LVGL task.
esp_err_t stock_quote_fetch(const char *symbol, stock_quote_t *out);

#ifdef __cplusplus
}
#endif
