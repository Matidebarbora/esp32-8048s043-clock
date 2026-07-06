#include "notification_screen.h"
#include "lcd.h"
#include "notification_store.h"
#include "notification_icon.h"
#include "es_fonts.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CARD_MARGIN 24

static lv_obj_t *s_clock_scr = nullptr;
static lv_obj_t *s_list      = nullptr;  // rebuilt on clear

static void on_home_click(lv_event_t *e)
{
    lv_obj_t *scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_clock_scr);
    lv_obj_del_async(scr);
}

static void format_relative_time(time_t received_at, char *out, size_t out_size)
{
    time_t now = time(nullptr);
    if (now < 0 || received_at <= 0 || now < received_at) {
        strlcpy(out, "", out_size);
        return;
    }
    long diff = (long)difftime(now, received_at);
    if (diff < 60)          strlcpy(out, "just now", out_size);
    else if (diff < 3600)   snprintf(out, out_size, "%ld min ago", diff / 60);
    else if (diff < 86400)  snprintf(out, out_size, "%ld hr ago", diff / 3600);
    else                    snprintf(out, out_size, "%ld d ago", diff / 86400);
}

static void build_notif_row(lv_obj_t *parent, const notification_t *n, bool is_last)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 14, 0);
    lv_obj_set_style_pad_hor(row, 24, 0);
    if (!is_last) {
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_make(46, 46, 52), 0);
    }
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 12, 0);

    lv_obj_t *left_row = lv_obj_create(row);
    lv_obj_remove_style_all(left_row);
    lv_obj_clear_flag(left_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(left_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(left_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(left_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(left_row, 12, 0);

    notification_icon_create(left_row, n->app_name, 36);

    lv_obj_t *text_col = lv_obj_create(left_row);
    lv_obj_remove_style_all(text_col);
    lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(text_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(text_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *app_lbl = lv_label_create(text_col);
    lv_label_set_text(app_lbl, n->app_name);
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_16_es, 0);
    lv_obj_set_style_text_color(app_lbl, lv_color_make(140, 140, 145), 0);

    lv_obj_t *title_lbl = lv_label_create(text_col);
    lv_label_set_text(title_lbl, n->title[0] ? n->title : "(no title)");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20_es, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);

    if (n->message[0]) {
        lv_obj_t *msg_lbl = lv_label_create(text_col);
        lv_label_set_text(msg_lbl, n->message);
        lv_obj_set_style_text_font(msg_lbl, &lv_font_montserrat_16_es, 0);
        lv_obj_set_style_text_color(msg_lbl, lv_color_make(170, 170, 175), 0);
        lv_obj_set_width(msg_lbl, 460);
        lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_DOT);
    }

    char time_buf[16];
    format_relative_time(n->received_at, time_buf, sizeof(time_buf));
    lv_obj_t *time_lbl = lv_label_create(row);
    lv_label_set_text(time_lbl, time_buf);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_make(120, 120, 125), 0);
}

static void populate_list()
{
    lv_obj_clean(s_list);

    notification_t items[NOTIF_STORE_MAX];
    size_t count = notification_store_get_all(items, NOTIF_STORE_MAX);

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "No notifications yet");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_make(160, 160, 160), 0);
        lv_obj_set_style_pad_all(empty, 24, 0);
        return;
    }

    for (size_t i = 0; i < count; i++)
        build_notif_row(s_list, &items[i], i == count - 1);
}

static void on_clear_all_click(lv_event_t *)
{
    notification_store_clear();
    populate_list();
}

void notification_screen_show(lv_obj_t *clock_scr)
{
    s_clock_scr = clock_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_make(8, 8, 10), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Notifications");
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

    lv_obj_t *clear_all = lv_btn_create(scr);
    lv_obj_set_size(clear_all, 44, 44);
    lv_obj_align(clear_all, LV_ALIGN_TOP_RIGHT, -64, 10);
    lv_obj_set_style_bg_opa(clear_all, LV_OPA_30, 0);
    lv_obj_t *clear_icon = lv_label_create(clear_all);
    lv_label_set_text(clear_icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(clear_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(clear_icon);
    lv_obj_add_event_cb(clear_all, on_clear_all_click, LV_EVENT_CLICKED, nullptr);

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_style_bg_color(s_list, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_list, 20, 0);
    lv_obj_set_style_border_width(s_list, 1, 0);
    lv_obj_set_style_border_color(s_list, lv_color_make(46, 46, 52), 0);
    lv_obj_set_size(s_list, LCD_H_RES - 2 * CARD_MARGIN, LCD_V_RES - 100);
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(s_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    populate_list();

    lv_disp_load_scr(scr);
}
