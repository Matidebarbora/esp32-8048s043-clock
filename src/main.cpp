#include <cstring>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"

#include "lcd.h"
#include "config.h"
#include "wifi_time.h"
#include "wifi_settings.h"
#include "wifi_store.h"
#include "app_settings.h"
#include "settings_screen.h"
#include "weather.h"
#include "forecast_screen.h"
#include "weather_icon.h"
#include "geolocation.h"
#include "time_digits_data.h"
#include "bulb_icon_data.h"
#include "dimmer_settings.h"

static const char *TAG = "main";

// ── Wi-Fi connect candidates: config.h networks + everything remembered
// via the settings screen, deduplicated by SSID ──────────────────────────────
#define MAX_CONNECT_CANDIDATES (8 + WIFI_STORE_MAX_NETWORKS)
static wifi_network_t g_connect_candidates[MAX_CONNECT_CANDIDATES];
static size_t         g_connect_candidate_count;

static void build_connect_candidates()
{
    g_connect_candidate_count = 0;

    size_t cfg_count = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    for (size_t i = 0; i < cfg_count && g_connect_candidate_count < MAX_CONNECT_CANDIDATES; i++)
        g_connect_candidates[g_connect_candidate_count++] = WIFI_NETWORKS[i];

    const wifi_saved_network_t *saved;
    size_t saved_count = wifi_store_get_all(&saved);
    for (size_t i = 0; i < saved_count && g_connect_candidate_count < MAX_CONNECT_CANDIDATES; i++) {
        bool dup = false;
        for (size_t j = 0; j < g_connect_candidate_count; j++) {
            if (strcmp(g_connect_candidates[j].ssid, saved[i].ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;
        g_connect_candidates[g_connect_candidate_count].ssid     = saved[i].ssid;
        g_connect_candidates[g_connect_candidate_count].password = saved[i].password;
        g_connect_candidate_count++;
    }
}

// ── UI state ──────────────────────────────────────────────────────────────────
static lv_obj_t *g_time_digits[8];  // H H : M M : S S — rendered as images, see time_digits_data.h
static lv_obj_t *g_date_label;
static lv_obj_t *g_status_label;
static lv_obj_t *g_wifi_label;
static lv_obj_t *g_temp_min_label;
static lv_obj_t *g_temp_now_label;
static lv_obj_t *g_temp_max_label;
static lv_obj_t *g_weather_icon_slot;
static lv_obj_t *g_weather_desc_label;
static lv_obj_t *g_rain_label;
static lv_obj_t *g_location_label;
static lv_obj_t *g_last_update_label;

// Horizontal margin left/right of every card, so the dark background shows
// as a frame around them.
#define CARD_MARGIN 24

// Dark-mode "surface" card: rounded rect, slightly lighter than the screen
// background, with a subtle border. Spans the full width minus CARD_MARGIN
// on each side. Children are laid out in `flow`, centered.
static lv_obj_t *make_card(lv_obj_t *parent, lv_flex_flow_t flow = LV_FLEX_FLOW_COLUMN)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_make(46, 46, 52), 0);
    lv_obj_set_size(card, LCD_H_RES - 2 * CARD_MARGIN, LV_SIZE_CONTENT);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, flow);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return card;
}

// Builds a [caption above value] column for the weather card and returns the
// value label so the caller can update it later.
static lv_obj_t *make_temp_col(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_clear_flag(col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cap = lv_label_create(col);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cap, lv_color_make(140, 140, 140), 0);

    lv_obj_t *val = lv_label_create(col);
    lv_label_set_text(val, "--\xc2\xb0" "C");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    return val;
}

// LVGL's built-in fonts top out at 48px, too small for a prominent clock
// face. The big time display is rendered from pre-generated digit images
// instead (Montserrat Regular, ~61px tall) — see time_digits_data.h.
static const lv_img_dsc_t *time_char_img(char c)
{
    switch (c) {
        case '0': return &img_time_0;
        case '1': return &img_time_1;
        case '2': return &img_time_2;
        case '3': return &img_time_3;
        case '4': return &img_time_4;
        case '5': return &img_time_5;
        case '6': return &img_time_6;
        case '7': return &img_time_7;
        case '8': return &img_time_8;
        case '9': return &img_time_9;
        case ':': return &img_time_colon;
        default:  return &img_time_dash;
    }
}

static void on_wifi_icon_click(lv_event_t *e)
{
    auto *clock_scr = (lv_obj_t *)lv_event_get_user_data(e);
    wifi_settings_show(clock_scr);
}

static void on_settings_icon_click(lv_event_t *e)
{
    auto *clock_scr = (lv_obj_t *)lv_event_get_user_data(e);
    settings_screen_show(clock_scr);
}

// ── Brightness: 100% (white bulb) / 40% (celeste bulb) ───────────────────────
// Manual (bulb icon tap) and automatic (dimmer_task, driven by the Dimmer
// setting) both go through set_brightness() so the icon color never gets out
// of sync with the actual backlight level.
static lv_obj_t *g_bulb_icon;
static bool      g_brightness_full = true;

static void refresh_bulb_icon()
{
    lv_color_t color = g_brightness_full ? lv_color_white() : lv_color_make(80, 220, 255);
    lv_obj_set_style_img_recolor(g_bulb_icon, color, 0);
    lv_obj_set_style_img_recolor_opa(g_bulb_icon, LV_OPA_COVER, 0);
}

// Safe to call from any task — takes the LVGL mutex itself.
static void set_brightness(bool full)
{
    g_brightness_full = full;
    lcd_set_backlight(full ? 100 : 40);
    lvgl_acquire();
    refresh_bulb_icon();
    lvgl_release();
}

static void on_brightness_click(lv_event_t *)
{
    set_brightness(!g_brightness_full);
}

static void on_weather_card_click(lv_event_t *e)
{
    auto *clock_scr = (lv_obj_t *)lv_event_get_user_data(e);
    forecast_screen_show(clock_scr);
}

static void build_ui(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_make(8, 8, 10), 0);

    lv_obj_t *wifi_btn = lv_btn_create(scr);
    lv_obj_set_size(wifi_btn, 44, 44);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_30, 0);
    lv_obj_t *wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(wifi_icon);
    lv_obj_add_event_cb(wifi_btn, on_wifi_icon_click, LV_EVENT_CLICKED, scr);

    lv_obj_t *gear = lv_btn_create(scr);
    lv_obj_set_size(gear, 44, 44);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -64, 10);
    lv_obj_set_style_bg_opa(gear, LV_OPA_30, 0);
    lv_obj_t *gear_icon = lv_label_create(gear);
    lv_label_set_text(gear_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gear_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(gear_icon);
    lv_obj_add_event_cb(gear, on_settings_icon_click, LV_EVENT_CLICKED, scr);

    lv_obj_t *bulb_btn = lv_btn_create(scr);
    lv_obj_set_size(bulb_btn, 44, 44);
    lv_obj_align(bulb_btn, LV_ALIGN_TOP_RIGHT, -118, 10);
    lv_obj_set_style_bg_opa(bulb_btn, LV_OPA_30, 0);
    g_bulb_icon = lv_img_create(bulb_btn);
    lv_img_set_src(g_bulb_icon, &img_bulb);
    lv_obj_center(g_bulb_icon);
    lv_obj_add_event_cb(bulb_btn, on_brightness_click, LV_EVENT_CLICKED, nullptr);
    refresh_bulb_icon();

    g_wifi_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(g_wifi_label, 280);
    lv_label_set_long_mode(g_wifi_label, LV_LABEL_LONG_DOT);
    lv_obj_align(g_wifi_label, LV_ALIGN_TOP_LEFT, 16, 18);
    lv_label_set_text(g_wifi_label, "");

    // ── Time + date card ─────────────────────────────────────────────────────
    lv_obj_t *time_card = make_card(scr);
    lv_obj_set_style_pad_hor(time_card, 48, 0);
    lv_obj_set_style_pad_ver(time_card, 24, 0);
    lv_obj_set_style_pad_row(time_card, 20, 0);
    lv_obj_align(time_card, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *time_row = lv_obj_create(time_card);
    lv_obj_remove_style_all(time_row);
    lv_obj_clear_flag(time_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(time_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_row, 2, 0);

    static const char PLACEHOLDER[9] = "--:--:--";
    for (int i = 0; i < 8; i++) {
        g_time_digits[i] = lv_img_create(time_row);
        lv_img_set_src(g_time_digits[i], time_char_img(PLACEHOLDER[i]));
    }

    g_date_label = lv_label_create(time_card);
    lv_obj_set_style_text_font(g_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_date_label, lv_color_make(170, 170, 175), 0);
    lv_label_set_text(g_date_label, "");

    // ── Weather card ─────────────────────────────────────────────────────────
    // Two groups, centered together as a unit:
    //   Group 1: [icon] [description / rain%]
    //   Group 2: [MIN|NOW|MAX] / [City - Last update: HH:MM]
    lv_obj_t *weather_card = make_card(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(weather_card, 24, 0);
    lv_obj_set_style_pad_ver(weather_card, 14, 0);
    lv_obj_set_style_pad_column(weather_card, 32, 0);  // gap BETWEEN the two groups
    lv_obj_align_to(weather_card, time_card, LV_ALIGN_OUT_BOTTOM_MID, 0, 24);
    // Tappable: opens the 5-day forecast screen. Give it a subtle pressed
    // color so it reads as tappable.
    lv_obj_set_style_bg_color(weather_card, lv_color_make(36, 36, 42), LV_STATE_PRESSED);
    lv_obj_add_event_cb(weather_card, on_weather_card_click, LV_EVENT_CLICKED, scr);

    // ── Group 1: icon + description/rain, packed tightly together ───────────
    // Every container below is purely for layout, so CLICKABLE is cleared on
    // all of them — otherwise LVGL's default hit testing finds the deepest
    // clickable object under the finger and the tap never bubbles up to
    // weather_card's own click handler.
    lv_obj_t *group1 = lv_obj_create(weather_card);
    lv_obj_remove_style_all(group1);
    lv_obj_clear_flag(group1, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(group1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(group1, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(group1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group1, 22, 0);  // gap between icon and description

    // Icon: fixed 100x100 image, spans the full height of the card's content area
    g_weather_icon_slot = lv_obj_create(group1);
    lv_obj_remove_style_all(g_weather_icon_slot);
    lv_obj_clear_flag(g_weather_icon_slot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(g_weather_icon_slot, 100, 100);

    // description / rain% — stacked
    lv_obj_t *desc_col = lv_obj_create(group1);
    lv_obj_remove_style_all(desc_col);
    lv_obj_clear_flag(desc_col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(desc_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(desc_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(desc_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(desc_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(desc_col, 4, 0);

    g_weather_desc_label = lv_label_create(desc_col);
    lv_obj_set_style_text_font(g_weather_desc_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_weather_desc_label, lv_color_make(190, 190, 195), 0);
    lv_label_set_text(g_weather_desc_label, "");

    lv_obj_t *rain_row = lv_obj_create(desc_col);
    lv_obj_remove_style_all(rain_row);
    lv_obj_clear_flag(rain_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(rain_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(rain_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(rain_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rain_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(rain_row, 8, 0);

    lv_obj_t *rain_icon = lv_label_create(rain_row);
    lv_label_set_text(rain_icon, LV_SYMBOL_TINT);
    lv_obj_set_style_text_font(rain_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(rain_icon, lv_color_make(90, 140, 200), 0);

    g_rain_label = lv_label_create(rain_row);
    lv_obj_set_style_text_font(g_rain_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_rain_label, lv_color_make(90, 140, 200), 0);
    lv_label_set_text(g_rain_label, "");

    // ── Group 2: MIN|NOW|MAX, with "City - Last update" stacked below it ────
    lv_obj_t *temp_group = lv_obj_create(weather_card);
    lv_obj_remove_style_all(temp_group);
    lv_obj_clear_flag(temp_group, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(temp_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(temp_group, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(temp_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(temp_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(temp_group, 8, 0);

    lv_obj_t *temp_row = lv_obj_create(temp_group);
    lv_obj_remove_style_all(temp_row);
    lv_obj_clear_flag(temp_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(temp_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(temp_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(temp_row, 14, 0);

    g_temp_min_label = make_temp_col(temp_row, "MIN");
    lv_obj_t *sep1 = lv_label_create(temp_row);
    lv_label_set_text(sep1, "|");
    lv_obj_set_style_text_font(sep1, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sep1, lv_color_make(80, 80, 80), 0);
    g_temp_now_label = make_temp_col(temp_row, "NOW");
    lv_obj_t *sep2 = lv_label_create(temp_row);
    lv_label_set_text(sep2, "|");
    lv_obj_set_style_text_font(sep2, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sep2, lv_color_make(80, 80, 80), 0);
    g_temp_max_label = make_temp_col(temp_row, "MAX");

    // "City - Last update: HH:MM", centered under MIN|NOW|MAX
    lv_obj_t *bottom_row = lv_obj_create(temp_group);
    lv_obj_remove_style_all(bottom_row);
    lv_obj_clear_flag(bottom_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(bottom_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(bottom_row, 1, 0);
    lv_obj_set_style_border_side(bottom_row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bottom_row, lv_color_make(46, 46, 52), 0);
    lv_obj_set_style_pad_top(bottom_row, 6, 0);
    lv_obj_set_layout(bottom_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bottom_row, 6, 0);

    g_location_label = lv_label_create(bottom_row);
    lv_obj_set_style_text_font(g_location_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_location_label, lv_color_make(140, 140, 145), 0);
    lv_label_set_text(g_location_label, "");

    lv_obj_t *update_sep = lv_label_create(bottom_row);
    lv_label_set_text(update_sep, "-");
    lv_obj_set_style_text_font(update_sep, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(update_sep, lv_color_make(90, 90, 95), 0);

    g_last_update_label = lv_label_create(bottom_row);
    lv_obj_set_style_text_font(g_last_update_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_last_update_label, lv_color_make(140, 140, 145), 0);
    lv_label_set_text(g_last_update_label, "");

    g_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_make(120, 120, 120), 0);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(g_status_label, "");
}

// Called by lv_timer every second — runs inside lv_task_handler, no mutex needed
static void clock_tick_cb(lv_timer_t *)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year <= 70) return;  // system clock not set yet

    char tbuf[12], dbuf[32];
    strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &ti);
    strftime(dbuf, sizeof(dbuf), "%A %d %B %Y", &ti);
    for (int i = 0; i < 8 && tbuf[i]; i++)
        lv_img_set_src(g_time_digits[i], time_char_img(tbuf[i]));
    lv_label_set_text(g_date_label, dbuf);
}

// Called from app_main (not LVGL task) — must use lvgl_acquire
static void set_status(const char *msg)
{
    lvgl_acquire();
    lv_label_set_text(g_status_label, msg);
    lvgl_release();
}

static void wifi_cb(const char *ssid, size_t index, size_t total)
{
    char buf[80];
    snprintf(buf, sizeof(buf), "Connecting WiFi\nTrying with %s", ssid);
    set_status(buf);
}

// Polls the actual Wi-Fi association state so the header reflects reality
// regardless of whether the connection came from startup or the settings
// screen. Runs inside lv_task_handler — no mutex needed.
static void wifi_status_tick_cb(lv_timer_t *)
{
    char ssid[33];
    if (wifi_get_connected_ssid(ssid, sizeof(ssid))) {
        lv_label_set_text_fmt(g_wifi_label, LV_SYMBOL_WIFI " %s", ssid);
        lv_obj_set_style_text_color(g_wifi_label, lv_color_make(60, 200, 100), 0);
    } else {
        lv_label_set_text(g_wifi_label, LV_SYMBOL_WIFI " No connection");
        lv_obj_set_style_text_color(g_wifi_label, lv_color_make(150, 70, 70), 0);
    }
}

// If the Wi-Fi connection is ever down (failed at boot, or dropped later),
// retry every 10 s. Runs on its own task since wifi_connect_any() blocks for
// up to its timeout; wifi_time.cpp serializes it against wifi_connect_one()
// (manual connect from the settings screen) so they never touch the driver
// at the same time.
static void wifi_reconnect_task(void *)
{
    char ssid[33];
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (wifi_get_connected_ssid(ssid, sizeof(ssid))) continue;

        ESP_LOGI(TAG, "Wi-Fi is down, retrying...");
        build_connect_candidates();  // pick up any network saved since boot
        if (wifi_connect_any(g_connect_candidates, g_connect_candidate_count, nullptr, 8000) == ESP_OK)
            ESP_LOGI(TAG, "Reconnected");
    }
}

// Applies the Dimmer setting (OFF/ON/AUTO). Mode changes always re-apply
// immediately (via the notify below). In AUTO, the dim/bright transition is
// edge-triggered — it only calls set_brightness() when the schedule zone
// actually changes, not on every check — so a manual bulb-icon override made
// between two scheduled transitions is left alone until the next one.
static TaskHandle_t s_dimmer_task_handle = nullptr;

static void on_dimmer_settings_changed()
{
    if (s_dimmer_task_handle) xTaskNotifyGive(s_dimmer_task_handle);
}

static void dimmer_task(void *)
{
    auto     last_mode  = (dimmer_mode_t)-1;  // force the first iteration to apply
    bool     last_zone_dim = false;
    bool     zone_known    = false;

    while (true) {
        dimmer_settings_t s = dimmer_settings_get();

        if (s.mode != last_mode) {
            if (s.mode == DIMMER_OFF)      set_brightness(true);
            else if (s.mode == DIMMER_ON)  set_brightness(false);
            last_mode  = s.mode;
            zone_known = false;  // re-evaluate AUTO's zone fresh below
        }

        if (s.mode == DIMMER_AUTO) {
            time_t now;
            struct tm ti;
            time(&now);
            localtime_r(&now, &ti);
            if (ti.tm_year > 70) {  // only once the clock is actually set
                int cur_min   = ti.tm_hour * 60 + ti.tm_min;
                int start_min = s.dim_start_hour * 60 + s.dim_start_min;
                int end_min   = s.dim_end_hour   * 60 + s.dim_end_min;
                bool in_dim_zone = (start_min <= end_min)
                    ? (cur_min >= start_min && cur_min < end_min)
                    : (cur_min >= start_min || cur_min < end_min);  // wraps past midnight

                if (!zone_known || in_dim_zone != last_zone_dim) {
                    set_brightness(!in_dim_zone);
                    last_zone_dim = in_dim_zone;
                    zone_known    = true;
                }
            }
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));  // recheck every 30s, or sooner if notified
    }
}

// Waits for Wi-Fi, then fetches the weather every 15 minutes. Runs on its own
// task since the HTTPS request blocks; UI updates go through lvgl_acquire().
static void weather_task(void *)
{
    char ssid[33];
    while (!wifi_get_connected_ssid(ssid, sizeof(ssid)))
        vTaskDelay(pdMS_TO_TICKS(2000));

    // wifi_get_connected_ssid() reports Wi-Fi association, not IP-level
    // readiness — DHCP (and therefore DNS) can still be a second or two
    // behind. Give it a moment before the first request.
    vTaskDelay(pdMS_TO_TICKS(3000));

    // One-time IP geolocation lookup — falls back to config.h's static
    // coordinates (no city name shown) if it fails.
    float lat = FALLBACK_LATITUDE, lon = FALLBACK_LONGITUDE;
    geo_location_t geo;
    if (geolocation_fetch(&geo) == ESP_OK) {
        lat = geo.lat;
        lon = geo.lon;
        lvgl_acquire();
        lv_label_set_text(g_location_label, geo.city);
        lvgl_release();
        ESP_LOGI(TAG, "Using IP-geolocated coordinates: %s (%.4f, %.4f)", geo.city, lat, lon);
    } else {
        ESP_LOGW(TAG, "Geolocation failed, using fallback coordinates (%.4f, %.4f)", lat, lon);
    }

    while (true) {
        weather_data_t wd;
        if (weather_fetch(lat, lon, &wd) == ESP_OK) {
            // lv_label_set_text_fmt() uses LVGL's own reduced-size snprintf,
            // which doesn't support %f by default (CONFIG_LV_SPRINTF_USE_FLOAT
            // is off) — it printed the literal text "f°" instead of a number.
            // Format with libc's snprintf (always float-capable) instead.
            char min_buf[12], now_buf[12], max_buf[12], rain_buf[16];
            snprintf(min_buf, sizeof(min_buf), "%.0f\xc2\xb0" "C", wd.min_c);
            snprintf(now_buf, sizeof(now_buf), "%.0f\xc2\xb0" "C", wd.current_c);
            snprintf(max_buf, sizeof(max_buf), "%.0f\xc2\xb0" "C", wd.max_c);

            int rain_pct = wd.daily_count > 0 ? wd.daily[0].precip_prob_max : -1;
            if (rain_pct >= 0) {
                snprintf(rain_buf, sizeof(rain_buf), "%d%%", rain_pct);
            } else {
                snprintf(rain_buf, sizeof(rain_buf), "--%%");
            }
            const char *desc = weather_code_description(wd.weather_code);

            char update_buf[24];
            time_t now_t;
            struct tm ti;
            time(&now_t);
            localtime_r(&now_t, &ti);
            if (ti.tm_year > 70) {
                strftime(update_buf, sizeof(update_buf), "Last update: %H:%M", &ti);
            } else {
                snprintf(update_buf, sizeof(update_buf), "Last update: --:--");
            }

            lvgl_acquire();
            lv_label_set_text(g_temp_min_label, min_buf);
            lv_label_set_text(g_temp_now_label, now_buf);
            lv_label_set_text(g_temp_max_label, max_buf);
            lv_obj_clean(g_weather_icon_slot);  // rebuild the icon shape for the new code
            weather_icon_create(g_weather_icon_slot, wd.weather_code, 100);
            lv_label_set_text(g_weather_desc_label, desc);
            lv_label_set_text(g_rain_label, rain_buf);
            lv_label_set_text(g_last_update_label, update_buf);
            weather_set_last(&wd);  // cache for forecast_screen.cpp — see weather.h
            lvgl_release();
            ESP_LOGI(TAG, "Weather: min=%.1f now=%.1f max=%.1f (%s, %s rain)",
                     wd.min_c, wd.current_c, wd.max_c, desc, rain_buf);
            vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000));
        } else {
            ESP_LOGW(TAG, "Weather fetch failed, retrying in 30 s");
            vTaskDelay(pdMS_TO_TICKS(30 * 1000));
        }
    }
}

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(LCDInit());
    lcd_set_backlight(100);  // matches g_brightness_full's initial value

    lv_obj_t *clock_scr = lv_scr_act();

    lvgl_acquire();
    build_ui(clock_scr);
    lv_timer_create(clock_tick_cb, 1000, NULL);
    lv_timer_create(wifi_status_tick_cb, 2000, NULL);
    lvgl_release();

    // Apply the saved DST mode before any time is read — NTP sync relies on
    // TZ already being set.
    app_settings_init();

    wifi_store_init();
    build_connect_candidates();

    // Must happen before spawning any task that might call wifi_connect_any()
    // or wifi_connect_one() (weather_task doesn't, but wifi_reconnect_task
    // does) — creates the mutex those calls serialize on.
    wifi_time_init();

    dimmer_settings_init();
    dimmer_settings_set_changed_cb(on_dimmer_settings_changed);

    xTaskCreate(weather_task, "weather", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(wifi_reconnect_task, "wifi_reconnect", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(dimmer_task, "dimmer", 3072, nullptr, tskIDLE_PRIORITY + 1, &s_dimmer_task_handle);

    // WiFi → NTP, 10 s total budget. No WiFi / no sync just leaves the time
    // and date blank — clock_tick_cb() already no-ops while the system clock
    // is unset (tm_year <= 70), so the digit images stay at their "--:--:--"
    // placeholder and the date label stays empty.
    esp_err_t wifi_ok = wifi_connect_any(g_connect_candidates, g_connect_candidate_count,
                                          wifi_cb, 10000);
    if (wifi_ok == ESP_OK) {
        set_status("Syncing time...");
        if (time_sync(app_settings_get_tz(), NTP_SERVER) == ESP_OK) {
            ESP_LOGI(TAG, "NTP sync OK, clock running");
        } else {
            ESP_LOGW(TAG, "NTP failed — leaving time unset");
        }
    } else {
        ESP_LOGW(TAG, "No WiFi — leaving time unset");
    }

    set_status("");  // clear "Syncing..." or any leftover status
    ESP_LOGI(TAG, "Clock running");

    vTaskSuspend(NULL);  // clock updates driven by lv_timer_create above
}
