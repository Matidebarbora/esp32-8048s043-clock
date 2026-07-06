#include "stocks_screen.h"
#include "lcd.h"
#include "stocks.h"
#include "stocks_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "stocks_screen";

static lv_obj_t *s_parent_scr = nullptr;
static lv_obj_t *s_search_ta  = nullptr;

// Points at the currently-shown list, or NULL once the screen has been torn
// down — same pattern as wifi_settings.cpp's s_active_list, so an in-flight
// search task can tell whether it's still safe to touch it.
static lv_obj_t *s_list = nullptr;

// Cached so rebuild_list() (called after every pin/unpin) can redraw the
// search-results section without re-querying the network.
static stock_search_result_t s_last_results[STOCK_SEARCH_MAX_RESULTS];
static size_t                s_last_result_count = 0;

// One-line status shown between the Pinned and Search-results sections
// ("Searching...", "No matches", "Search failed", or empty). Cleared
// whenever a search actually has results to show.
static char s_search_status[32] = "";

static void free_user_data_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void on_back_click(lv_event_t *e)
{
    lv_obj_t *this_scr = lv_obj_get_screen(lv_event_get_target(e));
    s_list = nullptr;
    lv_disp_load_scr(s_parent_scr);
    lv_obj_del_async(this_scr);
}

static void rebuild_list(void);  // forward decl — on_unpin_click/on_result_click call it

static void on_unpin_click(lv_event_t *e)
{
    char *symbol = (char *)lv_event_get_user_data(e);
    stocks_store_unpin(symbol);
    rebuild_list();
}

static void on_result_click(lv_event_t *e)
{
    char *symbol = (char *)lv_event_get_user_data(e);
    stocks_store_pin(symbol);
    rebuild_list();
}

// Redraws the whole list from current state: pinned symbols, then the
// status line and/or last search results (if any), each tappable. Safe to
// call from the LVGL task only — callers outside it must wrap with
// lvgl_acquire()/lvgl_release().
static void rebuild_list(void)
{
    if (!s_list) return;
    lv_obj_clean(s_list);

    const stocks_store_entry_t *pinned;
    size_t pinned_count = stocks_store_get_all(&pinned);

    lv_list_add_text(s_list, "Pinned (tap to remove)");
    if (pinned_count == 0) {
        lv_list_add_text(s_list, "  None yet");
    } else {
        for (size_t i = 0; i < pinned_count; i++) {
            char *sym_copy = strdup(pinned[i].symbol);
            lv_obj_t *btn  = lv_list_add_btn(s_list, nullptr, pinned[i].symbol);
            lv_obj_t *icon = lv_label_create(btn);
            lv_label_set_text(icon, LV_SYMBOL_TRASH);
            lv_obj_set_style_text_color(icon, lv_color_make(200, 70, 70), 0);
            lv_obj_add_event_cb(btn, on_unpin_click, LV_EVENT_CLICKED, sym_copy);
            lv_obj_add_event_cb(btn, free_user_data_cb, LV_EVENT_DELETE, sym_copy);
        }
    }

    if (s_search_status[0]) lv_list_add_text(s_list, s_search_status);

    if (s_last_result_count > 0) {
        lv_list_add_text(s_list, "Search results (tap to pin)");
        for (size_t i = 0; i < s_last_result_count; i++) {
            char label[96];
            snprintf(label, sizeof(label), "%s - %s",
                     s_last_results[i].symbol, s_last_results[i].name);

            bool already = stocks_store_is_pinned(s_last_results[i].symbol);
            char *sym_copy = strdup(s_last_results[i].symbol);
            lv_obj_t *btn  = lv_list_add_btn(s_list, nullptr, label);
            lv_obj_t *icon = lv_label_create(btn);
            if (already) {
                lv_label_set_text(icon, LV_SYMBOL_OK);
                lv_obj_set_style_text_color(icon, lv_color_make(60, 200, 100), 0);
            } else {
                lv_label_set_text(icon, LV_SYMBOL_PLUS);
                lv_obj_set_style_text_color(icon, lv_color_make(160, 160, 160), 0);
            }
            lv_obj_add_event_cb(btn, on_result_click, LV_EVENT_CLICKED, sym_copy);
            lv_obj_add_event_cb(btn, free_user_data_cb, LV_EVENT_DELETE, sym_copy);
        }
    }
}

struct SearchArgs {
    char query[64];
};

static void stock_search_task(void *arg)
{
    auto *args = (SearchArgs *)arg;

    stock_search_result_t results[STOCK_SEARCH_MAX_RESULTS];
    size_t    count = 0;
    esp_err_t ret    = stock_search(args->query, results, STOCK_SEARCH_MAX_RESULTS, &count);
    free(args);

    lvgl_acquire();
    if (ret == ESP_OK && count > 0) {
        memcpy(s_last_results, results, sizeof(stock_search_result_t) * count);
        s_last_result_count = count;
        s_search_status[0]  = '\0';
    } else {
        s_last_result_count = 0;
        strlcpy(s_search_status, ret != ESP_OK ? "Search failed" : "No matches",
                sizeof(s_search_status));
    }
    rebuild_list();
    lvgl_release();

    vTaskDelete(nullptr);
}

static void do_search(const char *query)
{
    if (!query[0] || !s_list) return;

    s_last_result_count = 0;
    strlcpy(s_search_status, "Searching...", sizeof(s_search_status));
    rebuild_list();

    auto *args = (SearchArgs *)malloc(sizeof(SearchArgs));
    strlcpy(args->query, query, sizeof(args->query));

    BaseType_t ok = xTaskCreate(stock_search_task, "stock_search", 6144, args,
                                tskIDLE_PRIORITY + 1, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn search task");
        free(args);
        strlcpy(s_search_status, "Internal error", sizeof(s_search_status));
        rebuild_list();
    }
}

static void on_ta_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY)
        do_search(lv_textarea_get_text(s_search_ta));
}

void stocks_screen_show(lv_obj_t *parent_scr)
{
    s_parent_scr        = parent_scr;
    s_last_result_count = 0;
    s_search_status[0]  = '\0';

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Stocks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 16);

    lv_obj_t *home = lv_btn_create(scr);
    lv_obj_set_size(home, 44, 44);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(home, LV_OPA_30, 0);
    lv_obj_t *home_icon = lv_label_create(home);
    lv_label_set_text(home_icon, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(home_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(home_icon);
    lv_obj_add_event_cb(home, on_back_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "Symbol or company name");
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
    lv_obj_set_size(ta, LCD_H_RES - 40, 50);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_add_event_cb(ta, on_ta_event, LV_EVENT_READY, nullptr);
    s_search_ta = ta;

    // Sits between the search box and the keyboard docked at the bottom
    // (200px tall, same as the Wi-Fi password screen's).
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LCD_H_RES - 40, 150);
    lv_obj_align_to(list, ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    s_list = list;
    rebuild_list();

    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_size(kb, LCD_H_RES, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Same flattened styling as the Wi-Fi password keyboard (wifi_settings.cpp)
    // — cuts the cost of this RGB panel's full 800x480 SW re-render per
    // keypress down to one redraw pass instead of several.
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    lv_color_t key_bg = lv_color_make(55, 55, 55);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_disp_load_scr(scr);
}
