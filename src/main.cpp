#include <cstring>
#include <cstdio>
#include <cmath>
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
#include "weather_icons_data.h"
#include "geolocation.h"
#include "time_digits_data.h"
#include "bulb_icon_data.h"
#include "dimmer_settings.h"
#include "ancs_client.h"
#include "nvs_flash.h"
#include "notification_store.h"
#include "notification_screen.h"
#include "notification_icon.h"
#include "es_fonts.h"
#include "notification_filter_settings.h"
#include "stocks.h"
#include "stocks_store.h"
#include "sd_card.h"
#include "photo_slideshow_screen.h"

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
static lv_obj_t *g_wifi_icon;  // header Wi-Fi button icon — green when connected, red otherwise
static lv_obj_t *g_temp_min_label;
static lv_obj_t *g_temp_now_label;
static lv_obj_t *g_temp_max_label;
static lv_obj_t *g_weather_icon_slot;
static lv_obj_t *g_weather_desc_label;
static lv_obj_t *g_rain_label;
static lv_obj_t *g_location_label;
static lv_obj_t *g_last_update_label;
static lv_obj_t *g_sun_marker;
static lv_obj_t *g_sunrise_label;
static lv_obj_t *g_sunset_label;
static lv_obj_t *g_notif_card;
static lv_obj_t *g_stock_card;

#define NOTIF_CARD_ROWS 3
#define STOCK_CARD_ROWS 3
#define STOCK_ROW_HEIGHT 26  // matches NOTIF_CARD_ROW_HEIGHT — same row rhythm, different card

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

// ── Sun-path arc geometry ─────────────────────────────────────────────────
// Full circle: the sun/moon's complete 24h orbit around the planet. 0°/180°
// (right/left) sit on the horizon — the day arc runs 180°→0° over the top
// from sunrise to sunset, the night arc continues 0°→-180° under the bottom
// from sunset back to the next sunrise. Shared by the dot-trail builder
// (build_ui) and the live sun-marker positioner (update_sun_arc, called
// every clock tick).
#define SUN_ARC_CX     90
#define SUN_ARC_CY     66
#define SUN_ARC_RADIUS 58

static void arc_point(float theta_deg, int *x, int *y)
{
    float rad = theta_deg * 3.14159265358979323846f / 180.0f;
    *x = SUN_ARC_CX + (int)(SUN_ARC_RADIUS * cosf(rad));
    *y = SUN_ARC_CY - (int)(SUN_ARC_RADIUS * sinf(rad));
}

// One small dashed-arc dot, absolutely positioned along the circle.
static void add_arc_dot(lv_obj_t *parent, float theta_deg)
{
    int x, y;
    arc_point(theta_deg, &x, &y);
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(dot, 4, 4);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_make(70, 70, 78), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(dot, x - 2, y - 2);
}

static int hhmm_to_minutes(const char *hhmm)
{
    int h = 0, m = 0;
    if (sscanf(hhmm, "%d:%d", &h, &m) != 2) return -1;
    return h * 60 + m;
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

static void on_time_card_click(lv_event_t *e)
{
    auto *clock_scr = (lv_obj_t *)lv_event_get_user_data(e);
    photo_slideshow_screen_show(clock_scr);
}

static void on_notification_card_click(lv_event_t *e)
{
    auto *clock_scr = (lv_obj_t *)lv_event_get_user_data(e);
    notification_screen_show(clock_scr);
}

// One compact row inside the persistent notification card: small icon +
// "AppName: title" on a single line, dot-truncated if too long.
#define NOTIF_CARD_ROW_HEIGHT 26  // matches the row icon size — every row/placeholder is exactly this tall

static void build_notif_card_row(lv_obj_t *parent, const notification_t *n)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(row, LV_SIZE_CONTENT, NOTIF_CARD_ROW_HEIGHT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    notification_icon_create(row, n->app_name, NOTIF_CARD_ROW_HEIGHT);

    const char *body = n->title[0] ? n->title : n->message;
    char buf[192];  // generous headroom (app_name[32] + message[128]) so -Werror=format-truncation stays quiet
    snprintf(buf, sizeof(buf), "%s: %s", n->app_name, body);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16_es, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_width(lbl, LCD_H_RES - 2 * CARD_MARGIN - 32 - NOTIF_CARD_ROW_HEIGHT - 10);  // card minus its own padding, icon, and gap
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
}

// A row slot with no notification yet — same fixed height as a real row, so
// the card's total height never changes as notifications arrive/clear.
// text is only non-null for the very first slot when the store is empty.
static void build_notif_card_empty_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(row, LV_SIZE_CONTENT, NOTIF_CARD_ROW_HEIGHT);
    if (!text) return;

    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(120, 120, 125), 0);
}

// Refreshes the persistent notification card from the store. Always renders
// exactly NOTIF_CARD_ROWS row slots (real notifications first, then empty
// placeholders) so the card's height never changes as notifications arrive
// or get cleared — only the weather/status labels above it were designed to
// tolerate the layout shifting under them, this card wasn't. Safe to call
// from the LVGL task only — callers outside it must wrap with
// lvgl_acquire()/lvgl_release() (see on_notification_store_changed()).
static void update_notification_card()
{
    lv_obj_clean(g_notif_card);

    notification_t items[NOTIF_CARD_ROWS];
    size_t count = notification_store_get_all(items, NOTIF_CARD_ROWS);

    for (size_t i = 0; i < NOTIF_CARD_ROWS; i++) {
        if (i < count)
            build_notif_card_row(g_notif_card, &items[i]);
        else
            build_notif_card_empty_row(g_notif_card, (count == 0 && i == 0) ? "No notifications yet" : nullptr);
    }
}

// Called from the ANCS BLE task (via notification_store_set_changed_cb) —
// not the LVGL task, so it must take the LVGL mutex itself.
static void on_notification_store_changed()
{
    lvgl_acquire();
    update_notification_card();
    lvgl_release();
}

// One row in the stock card: up/down arrow (colored) + symbol + price, all
// in white except the arrow. Neutral gray arrow and "--" price while a quote
// hasn't been fetched yet (valid == false).
static void build_stock_card_row(lv_obj_t *parent, const stock_quote_t *q)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(row, LV_PCT(100), STOCK_ROW_HEIGHT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_clear_flag(left, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left, 6, 0);

    lv_obj_t *arrow = lv_label_create(left);
    lv_label_set_text(arrow, q->valid && !q->up ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow,
        !q->valid ? lv_color_make(120, 120, 125)
        : q->up   ? lv_color_make(60, 200, 100)
                  : lv_color_make(200, 70, 70), 0);

    lv_obj_t *sym = lv_label_create(left);
    lv_label_set_text(sym, q->symbol);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sym, lv_color_white(), 0);

    lv_obj_t *price = lv_label_create(row);
    char buf[16];
    if (q->valid) snprintf(buf, sizeof(buf), "%.2f", q->price);
    else strlcpy(buf, "--", sizeof(buf));
    lv_label_set_text(price, buf);
    lv_obj_set_style_text_font(price, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(price, lv_color_white(), 0);
}

// A row slot with no pinned stock yet — same fixed height as a real row, so
// the card's total height never changes as stocks are pinned/unpinned.
static void build_stock_card_empty_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(row, LV_PCT(100), STOCK_ROW_HEIGHT);
    if (!text) return;

    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(120, 120, 125), 0);
}

// Rebuilds the stock card from quotes (up to STOCK_CARD_ROWS entries).
// Always renders exactly STOCK_CARD_ROWS row slots, same fixed-height-card
// rationale as update_notification_card(). Called from stocks_task (not the
// LVGL task) — caller must wrap with lvgl_acquire()/lvgl_release().
static void update_stock_card(const stock_quote_t *quotes, size_t count)
{
    lv_obj_clean(g_stock_card);

    for (size_t i = 0; i < STOCK_CARD_ROWS; i++) {
        if (i < count)
            build_stock_card_row(g_stock_card, &quotes[i]);
        else
            build_stock_card_empty_row(g_stock_card, (count == 0 && i == 0) ? "No stocks pinned" : nullptr);
    }
}

static void build_ui(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, lv_color_make(8, 8, 10), 0);

    lv_obj_t *wifi_btn = lv_btn_create(scr);
    lv_obj_set_size(wifi_btn, 44, 44);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_30, 0);
    g_wifi_icon = lv_label_create(wifi_btn);
    lv_label_set_text(g_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(g_wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_wifi_icon, lv_color_make(150, 70, 70), 0);  // red until first status tick confirms a connection
    lv_obj_center(g_wifi_icon);
    lv_obj_add_event_cb(wifi_btn, on_wifi_icon_click, LV_EVENT_CLICKED, scr);

    // Sits where the Wi-Fi SSID label used to be, top-left of the header.
    g_date_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_date_label, lv_color_make(170, 170, 175), 0);
    lv_obj_set_width(g_date_label, 400);
    lv_label_set_long_mode(g_date_label, LV_LABEL_LONG_DOT);
    lv_obj_align(g_date_label, LV_ALIGN_TOP_LEFT, CARD_MARGIN, 16);  // left edge lines up with the cards below
    lv_label_set_text(g_date_label, "");

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

    // ── Time card (fixed position/size, left side of the header row) ───────────
    // Width is fixed (not LV_SIZE_CONTENT) so it doesn't jitter as digit
    // images of varying width (e.g. "1" vs "8") cycle through — sized to fit
    // the widest possible HH:MM:SS combination (six digits up to 60px each +
    // two 22px colons = 404px of images) plus the card's own horizontal
    // padding, with a little headroom.
    const int time_digit_height = 70;
    const int header_row_h      = time_digit_height + 2 * 16;                  // 102
    const int header_row_y      = LCD_V_RES / 2 - 119 - header_row_h / 2;      // matches the old CENTER/-119 vertical position
    const int header_gap        = 12;                                          // gap between header cards, and down to the weather card
    const int time_card_w       = 470;

    lv_obj_t *time_card = make_card(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(time_card, time_card_w, header_row_h);
    lv_obj_set_style_pad_hor(time_card, 24, 0);
    lv_obj_set_style_pad_ver(time_card, 16, 0);
    lv_obj_set_flex_align(time_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(time_card, LV_ALIGN_TOP_LEFT, CARD_MARGIN, header_row_y);
    // Tappable: opens the photo slideshow (photo_slideshow_screen.cpp).
    lv_obj_set_style_bg_color(time_card, lv_color_make(36, 36, 42), LV_STATE_PRESSED);
    lv_obj_add_event_cb(time_card, on_time_card_click, LV_EVENT_CLICKED, scr);

    lv_obj_t *time_row = lv_obj_create(time_card);
    lv_obj_remove_style_all(time_row);
    lv_obj_clear_flag(time_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(time_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(time_row, 0, 0);

    static const char PLACEHOLDER[9] = "--:--:--";
    for (int i = 0; i < 8; i++) {
        g_time_digits[i] = lv_img_create(time_row);
        lv_img_set_src(g_time_digits[i], time_char_img(PLACEHOLDER[i]));
    }

    // ── Stock card: fills the rest of the header row, right of the time card.
    // Up to STOCK_CARD_ROWS pinned symbols, one compact row each — rebuilt
    // from scratch on every update (see update_stock_card()), same
    // clean+rebuild pattern as the notification card below.
    g_stock_card = make_card(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(g_stock_card, LCD_H_RES - 2 * CARD_MARGIN - time_card_w - header_gap, header_row_h);
    lv_obj_set_style_pad_hor(g_stock_card, 16, 0);
    lv_obj_set_style_pad_ver(g_stock_card, 6, 0);
    lv_obj_set_style_pad_row(g_stock_card, 4, 0);
    lv_obj_set_flex_align(g_stock_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align_to(g_stock_card, time_card, LV_ALIGN_OUT_RIGHT_MID, header_gap, 0);

    // ── Weather card ─────────────────────────────────────────────────────────
    // weather_card is a ROW: [left_spacer] [middle_col] [right_spacer].
    //   left_spacer / right_spacer both use flex-grow:1, so LVGL always makes
    //   them exactly the same width (whatever's left over once middle_col
    //   takes its content width) — that keeps Group 2 truly centered on the
    //   card regardless of middle_col's width, with Group 1 and Group 3 each
    //   centered in their own leftover half, no manual pixel math needed.
    //   Group 1 (in left_spacer): icon alone
    //   Group 2 (middle_col): description/rain% over [MIN|NOW|MAX] / [City - Last update: HH:MM]
    //   Group 3 (in right_spacer): dashed arc from sunrise to sunset with a moving sun
    lv_obj_t *weather_card = make_card(scr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(weather_card, 16, 0);
    lv_obj_set_style_pad_top(weather_card, 6, 0);
    lv_obj_set_style_pad_bottom(weather_card, 10, 0);
    lv_obj_set_style_pad_column(weather_card, 24, 0);  // gap between the 3 top-level groups
    // Anchored to the header row's fixed geometry (not time_card, which no
    // longer spans the full width) so it stays centered under both header cards.
    lv_obj_align(weather_card, LV_ALIGN_TOP_MID, 0, header_row_y + header_row_h + header_gap);
    // Tappable: opens the 5-day forecast screen. Give it a subtle pressed
    // color so it reads as tappable.
    lv_obj_set_style_bg_color(weather_card, lv_color_make(36, 36, 42), LV_STATE_PRESSED);
    lv_obj_add_event_cb(weather_card, on_weather_card_click, LV_EVENT_CLICKED, scr);

    // Every container below is purely for layout, so CLICKABLE is cleared on
    // all of them — otherwise LVGL's default hit testing finds the deepest
    // clickable object under the finger and the tap never bubbles up to
    // weather_card's own click handler.

    // ── Group 1: icon alone, centered in the left leftover space ────────────
    lv_obj_t *left_spacer = lv_obj_create(weather_card);
    lv_obj_remove_style_all(left_spacer);
    lv_obj_clear_flag(left_spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(left_spacer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left_spacer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_spacer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_spacer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(left_spacer, 1);

    g_weather_icon_slot = lv_obj_create(left_spacer);
    lv_obj_remove_style_all(g_weather_icon_slot);
    lv_obj_clear_flag(g_weather_icon_slot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(g_weather_icon_slot, 100, 100);

    // ── Group 2: description/rain over temps, centered on the card ──────────
    lv_obj_t *middle_col = lv_obj_create(weather_card);
    lv_obj_remove_style_all(middle_col);
    lv_obj_clear_flag(middle_col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(middle_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(middle_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(middle_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(middle_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(middle_col, 14, 0);  // gap between the two rows inside group 2

    // Description / rain% — stacked
    lv_obj_t *desc_col = lv_obj_create(middle_col);
    lv_obj_remove_style_all(desc_col);
    lv_obj_clear_flag(desc_col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(desc_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(desc_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(desc_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(desc_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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

    // MIN|NOW|MAX, with "City - Last update" stacked below it
    lv_obj_t *temp_group = lv_obj_create(middle_col);
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

    // ── Group 3: sun-path arc, centered in the right leftover space ─────────
    // right_spacer's only child is the circle itself now, so weather_card's
    // own cross-axis centering lands exactly on the circle — no extra
    // content below it to throw the centering off.
    lv_obj_t *right_spacer = lv_obj_create(weather_card);
    lv_obj_remove_style_all(right_spacer);
    lv_obj_clear_flag(right_spacer, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(right_spacer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(right_spacer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_spacer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_spacer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_flex_grow(right_spacer, 1);

    // Full circle: the sun/moon's complete orbit around the planet over 24h.
    // The horizon (sunrise/sunset) sits on the circle's horizontal diameter,
    // which is exactly the container's vertical center — see SUN_ARC_CY.
    lv_obj_t *arc_area = lv_obj_create(right_spacer);
    lv_obj_remove_style_all(arc_area);
    lv_obj_clear_flag(arc_area, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(arc_area, 2 * SUN_ARC_CX, 2 * SUN_ARC_CY);

    static const float ARC_DOT_ANGLES[] = {
        180, 165, 150, 135, 120, 105, 90, 75, 60, 45, 30, 15, 0,
        -15, -30, -45, -60, -75, -90, -105, -120, -135, -150, -165
    };
    for (float angle : ARC_DOT_ANGLES) add_arc_dot(arc_area, angle);

    g_sun_marker = lv_img_create(arc_area);
    lv_img_set_src(g_sun_marker, &img_weather_sun_32);
    lv_obj_add_flag(g_sun_marker, LV_OBJ_FLAG_HIDDEN);  // shown by update_sun_arc() once sun times are known

    // Sun times, back at the horizon line (the circle's vertical center) —
    // an opaque (not translucent) rectangle behind each one so the dashed
    // dots don't show through and clutter the text.
    g_sunrise_label = lv_label_create(arc_area);
    lv_obj_set_style_text_font(g_sunrise_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_sunrise_label, lv_color_make(140, 140, 145), 0);
    lv_obj_set_style_bg_color(g_sunrise_label, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(g_sunrise_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_sunrise_label, 0, 0);
    lv_obj_set_style_pad_hor(g_sunrise_label, 4, 0);
    lv_obj_set_style_pad_ver(g_sunrise_label, 2, 0);
    lv_label_set_text(g_sunrise_label, "--:--");
    lv_obj_align(g_sunrise_label, LV_ALIGN_LEFT_MID, 0, 0);

    g_sunset_label = lv_label_create(arc_area);
    lv_obj_set_style_text_font(g_sunset_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_sunset_label, lv_color_make(140, 140, 145), 0);
    lv_obj_set_style_bg_color(g_sunset_label, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(g_sunset_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_sunset_label, 0, 0);
    lv_obj_set_style_pad_hor(g_sunset_label, 4, 0);
    lv_obj_set_style_pad_ver(g_sunset_label, 2, 0);
    lv_label_set_text(g_sunset_label, "--:--");
    lv_obj_align(g_sunset_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Notification card ────────────────────────────────────────────────────
    // Up to the 3 most recent ANCS notifications, one compact row each —
    // rebuilt from scratch on every update (see update_notification_card()),
    // same clean+rebuild pattern as the weather icon slot above.
    g_notif_card = make_card(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(g_notif_card, 16, 0);
    lv_obj_set_style_pad_ver(g_notif_card, 6, 0);
    lv_obj_set_style_pad_row(g_notif_card, 4, 0);
    lv_obj_set_flex_align(g_notif_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align_to(g_notif_card, weather_card, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_bg_color(g_notif_card, lv_color_make(36, 36, 42), LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_notif_card, on_notification_card_click, LV_EVENT_CLICKED, scr);

    g_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_make(120, 120, 120), 0);
    lv_obj_align(g_status_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(g_status_label, "");

    update_notification_card();     // initial empty state
    update_stock_card(nullptr, 0);  // initial empty state
}

// Tracks the last (weather_code, is_night) pair the Group-1 icon was built
// for, so it's only rebuilt on an actual change instead of every tick.
static int  s_icon_weather_code = -999;  // sentinel — never a real WMO code
static bool s_icon_is_night     = false;

// Repositions the moving sun/moon marker along the full day/night orbit,
// refreshes the sunrise/sunset labels, and keeps the Group-1 weather icon's
// sun/moon variant in sync with the actual time of day — based on the
// cached weather data and the live clock. Cheap (no network) — called every
// second from clock_tick_cb() so the marker glides smoothly around the day.
static void update_sun_arc(const struct tm *ti)
{
    weather_data_t wd;
    if (!weather_get_last(&wd)) return;

    lv_label_set_text(g_sunrise_label, wd.sunrise);
    lv_label_set_text(g_sunset_label, wd.sunset);

    int  sunrise_min    = hhmm_to_minutes(wd.sunrise);
    int  sunset_min     = hhmm_to_minutes(wd.sunset);
    int  cur_min        = ti->tm_hour * 60 + ti->tm_min;
    bool have_sun_times = sunrise_min >= 0 && sunset_min >= 0 && sunset_min > sunrise_min;
    bool is_night       = have_sun_times && (cur_min < sunrise_min || cur_min >= sunset_min);

    // Sun icon by day, moon icon by night — rebuilt only when the code or
    // the day/night state actually changes.
    if (wd.weather_code != s_icon_weather_code || is_night != s_icon_is_night) {
        s_icon_weather_code = wd.weather_code;
        s_icon_is_night     = is_night;
        lv_obj_clean(g_weather_icon_slot);
        weather_icon_create(g_weather_icon_slot, wd.weather_code, 100, is_night);
    }

    if (!have_sun_times) {
        lv_obj_add_flag(g_sun_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Full 24h loop: 180°→0° over the top from sunrise to sunset (day), then
    // 0°→-180° under the bottom from sunset back to the next sunrise (night).
    float theta;
    if (!is_night) {
        float frac = (float)(cur_min - sunrise_min) / (float)(sunset_min - sunrise_min);
        theta = 180.0f * (1.0f - frac);
    } else {
        int night_len    = 1440 - (sunset_min - sunrise_min);
        int since_sunset = cur_min - sunset_min;
        if (since_sunset < 0) since_sunset += 1440;  // wrapped past midnight
        float frac = (float)since_sunset / (float)night_len;
        theta = -180.0f * frac;
    }

    int x, y;
    arc_point(theta, &x, &y);
    lv_img_set_src(g_sun_marker, is_night ? &img_weather_moon_32 : &img_weather_sun_32);
    lv_obj_clear_flag(g_sun_marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(g_sun_marker, x - 16, y - 16);  // 32x32 icon, centered on the arc point
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
    update_sun_arc(&ti);
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
// screen. Runs inside lv_task_handler — no mutex needed. Which network it's
// connected to is shown inside the Wi-Fi settings screen, not the header.
static void wifi_status_tick_cb(lv_timer_t *)
{
    char ssid[33];
    bool connected = wifi_get_connected_ssid(ssid, sizeof(ssid));
    lv_obj_set_style_text_color(g_wifi_icon,
        connected ? lv_color_make(60, 200, 100) : lv_color_make(150, 70, 70), 0);
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
            // Group-1 icon is rebuilt by update_sun_arc() on the next clock
            // tick, once weather_set_last() below makes this fetch visible —
            // that's also where the sun/moon (day/night) variant is decided.
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

static TaskHandle_t s_stocks_task_handle = nullptr;

// Called from stocks_screen.cpp (via stocks_store_set_changed_cb) whenever a
// symbol is pinned/unpinned — same edge-triggered wake pattern as
// on_dimmer_settings_changed(), so the card updates immediately instead of
// waiting up to a minute for the next periodic refresh.
static void on_stocks_store_changed()
{
    if (s_stocks_task_handle) xTaskNotifyGive(s_stocks_task_handle);
}

// Waits for Wi-Fi, then fetches quotes for the pinned symbols every 60 s (or
// immediately after a pin/unpin — see on_stocks_store_changed()). Runs on
// its own task since each HTTPS request blocks; UI updates go through
// lvgl_acquire().
static void stocks_task(void *)
{
    char ssid[33];
    while (!wifi_get_connected_ssid(ssid, sizeof(ssid)))
        vTaskDelay(pdMS_TO_TICKS(2000));

    while (true) {
        const stocks_store_entry_t *pinned;
        size_t pinned_count = stocks_store_get_all(&pinned);
        if (pinned_count > STOCK_CARD_ROWS) pinned_count = STOCK_CARD_ROWS;

        stock_quote_t quotes[STOCK_CARD_ROWS];
        for (size_t i = 0; i < pinned_count; i++) {
            if (stock_quote_fetch(pinned[i].symbol, &quotes[i]) != ESP_OK)
                ESP_LOGW(TAG, "Quote fetch failed for %s", pinned[i].symbol);
        }

        lvgl_acquire();
        update_stock_card(quotes, pinned_count);
        lvgl_release();

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60 * 1000));  // recheck every 60s, or sooner if notified
    }
}

extern "C" void app_main(void)
{
    // Must run before ancs_client_init() — Bluedroid needs NVS up front to
    // persist BLE bonding data, otherwise the iPhone has to be re-paired on
    // every reboot. Safe to call again later (wifi_store/app_settings/etc.
    // all do); nvs_flash_init() is idempotent.
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(LCDInit());
    lcd_set_backlight(100);  // matches g_brightness_full's initial value

    // Independent of Wi-Fi/BLE — just mounts the microSD card (if any) for
    // photo_slideshow_screen.cpp. Doesn't block boot if no card is present.
    sd_card_init();

    // Must happen before ancs_client_init() — a notification could in theory
    // arrive as soon as the BLE stack is up, and it needs to know which
    // apps are filtered.
    notification_filter_settings_init();

    ancs_client_init();  // BLE + ANCS (iPhone notification bridge) — see ancs_client.cpp

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

    notification_store_set_changed_cb(on_notification_store_changed);

    stocks_store_init();
    stocks_store_set_changed_cb(on_stocks_store_changed);

    xTaskCreate(weather_task, "weather", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
    xTaskCreate(stocks_task, "stocks", 8192, nullptr, tskIDLE_PRIORITY + 1, &s_stocks_task_handle);
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
