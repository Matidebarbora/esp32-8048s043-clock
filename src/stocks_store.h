#pragma once
#include "stocks.h"  // STOCK_SYMBOL_LEN
#include <stddef.h>
#include <stdbool.h>

#define STOCKS_STORE_MAX 3

typedef struct {
    char symbol[STOCK_SYMBOL_LEN];
} stocks_store_entry_t;

typedef void (*stocks_store_changed_cb_t)(void);

#ifdef __cplusplus
extern "C" {
#endif

// Initializes NVS (safe to call even if already initialized elsewhere) and
// loads previously-pinned symbols into RAM. Call once at startup.
void stocks_store_init(void);

// Returns a pointer to the in-RAM pinned-symbol list (valid until the next
// stocks_store_pin()/stocks_store_unpin() call) and the number of entries.
size_t stocks_store_get_all(const stocks_store_entry_t **out);

bool stocks_store_is_pinned(const char *symbol);

// Pins symbol and persists to NVS immediately, notifying the changed
// callback. If already at STOCKS_STORE_MAX, the oldest pinned symbol is
// evicted first (same convention as wifi_store's saved-network list).
void stocks_store_pin(const char *symbol);

// Unpins symbol (no-op if not pinned) and persists to NVS immediately,
// notifying the changed callback.
void stocks_store_unpin(const char *symbol);

// Called whenever the pinned list changes (pin or unpin) — main.cpp uses
// this to wake stocks_task for an immediate quote refresh.
void stocks_store_set_changed_cb(stocks_store_changed_cb_t cb);

#ifdef __cplusplus
}
#endif
