#include "forecast_screen.h"
#include "lcd.h"
#include "weather.h"
#include "weather_icon.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CARD_MARGIN 24

static lv_obj_t *s_clock_scr = nullptr;

static void on_home_click(lv_event_t *e)
{
    lv_obj_t *settings_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_clock_scr);
    lv_obj_del_async(settings_scr);
}

// "YYYY-MM-DD" -> "Wed 07/02". Falls back to the raw string on parse failure.
static void format_day_label(const char *iso_date, char *out, size_t out_size)
{
    int y, m, d;
    if (sscanf(iso_date, "%d-%d-%d", &y, &m, &d) == 3) {
        struct tm ti = {};
        ti.tm_year = y - 1900;
        ti.tm_mon  = m - 1;
        ti.tm_mday = d;
        ti.tm_hour = 12;  // clear of any DST/timezone edge effects
        mktime(&ti);      // normalizes tm_wday
        strftime(out, out_size, "%a %d/%m", &ti);
    } else {
        strlcpy(out, iso_date, out_size);
    }
}

static void build_day_row(lv_obj_t *parent, const weather_daily_t *d, bool is_today, bool is_last)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 24, 0);
    if (!is_last) {
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_make(46, 46, 52), 0);
    }
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left_row = lv_obj_create(row);
    lv_obj_remove_style_all(left_row);
    lv_obj_clear_flag(left_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(left_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(left_row, 12, 0);

    weather_icon_create(left_row, d->weather_code, 32);

    char day_buf[16];
    format_day_label(d->date, day_buf, sizeof(day_buf));

    lv_obj_t *day_lbl = lv_label_create(left_row);
    lv_label_set_text(day_lbl, is_today ? "Today" : day_buf);
    lv_obj_set_style_text_font(day_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(day_lbl, lv_color_white(), 0);

    lv_obj_t *right_col = lv_obj_create(row);
    lv_obj_remove_style_all(right_col);
    lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(right_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(right_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

    char temp_buf[24];
    snprintf(temp_buf, sizeof(temp_buf), "%.0f\xc2\xb0" "C  /  %.0f\xc2\xb0" "C", d->min_c, d->max_c);
    lv_obj_t *temp_lbl = lv_label_create(right_col);
    lv_label_set_text(temp_lbl, temp_buf);
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_make(170, 170, 175), 0);

    if (d->precip_prob_max >= 0) {
        lv_obj_t *rain_row = lv_obj_create(right_col);
        lv_obj_remove_style_all(rain_row);
        lv_obj_clear_flag(rain_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(rain_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(rain_row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(rain_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rain_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(rain_row, 6, 0);

        lv_obj_t *rain_icon = lv_label_create(rain_row);
        lv_label_set_text(rain_icon, LV_SYMBOL_TINT);
        lv_obj_set_style_text_font(rain_icon, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rain_icon, lv_color_make(90, 140, 200), 0);

        char rain_buf[16];
        snprintf(rain_buf, sizeof(rain_buf), "%d%%", d->precip_prob_max);
        lv_obj_t *rain_lbl = lv_label_create(rain_row);
        lv_label_set_text(rain_lbl, rain_buf);
        lv_obj_set_style_text_font(rain_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rain_lbl, lv_color_make(90, 140, 200), 0);
    }
}

void forecast_screen_show(lv_obj_t *clock_scr)
{
    s_clock_scr = clock_scr;

    weather_data_t wd;
    bool have_data = weather_get_last(&wd);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_make(8, 8, 10), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "5-Day Forecast");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 16);

    lv_obj_t *home = lv_btn_create(scr);
    lv_obj_set_size(home, 44, 44);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(home, LV_OPA_30, 0);
    lv_obj_t *home_icon = lv_label_create(home);
    lv_label_set_text(home_icon, LV_SYMBOL_HOME);
    lv_obj_set_style_text_font(home_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(home_icon);
    lv_obj_add_event_cb(home, on_home_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_make(46, 46, 52), 0);
    lv_obj_set_size(card, LCD_H_RES - 2 * CARD_MARGIN, LV_SIZE_CONTENT);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);

    if (!have_data || wd.daily_count == 0) {
        lv_obj_t *empty = lv_label_create(card);
        lv_label_set_text(empty, "No forecast data yet");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_make(160, 160, 160), 0);
        lv_obj_set_style_pad_all(empty, 24, 0);
    } else {
        for (int i = 0; i < wd.daily_count; i++)
            build_day_row(card, &wd.daily[i], i == 0, i == wd.daily_count - 1);
    }

    lv_disp_load_scr(scr);
}
