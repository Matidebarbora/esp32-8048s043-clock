#include "season_time_screen.h"
#include "lcd.h"
#include "app_settings.h"

static lv_obj_t *s_parent_scr  = nullptr;
static lv_obj_t *s_winter_btn  = nullptr;
static lv_obj_t *s_summer_btn  = nullptr;

static const lv_color_t COLOR_ACTIVE   = LV_COLOR_MAKE(50, 120, 200);
static const lv_color_t COLOR_INACTIVE = LV_COLOR_MAKE(60, 60, 60);

static void refresh_dst_buttons()
{
    dst_mode_t mode = app_settings_get_dst_mode();
    lv_obj_set_style_bg_color(s_winter_btn, mode == DST_WINTER ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
    lv_obj_set_style_bg_color(s_summer_btn, mode == DST_SUMMER ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
}

static void on_winter_click(lv_event_t *)
{
    app_settings_set_dst_mode(DST_WINTER);
    refresh_dst_buttons();
}

static void on_summer_click(lv_event_t *)
{
    app_settings_set_dst_mode(DST_SUMMER);
    refresh_dst_buttons();
}

static void on_back_click(lv_event_t *e)
{
    lv_obj_t *this_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_parent_scr);
    lv_obj_del_async(this_scr);
}

void season_time_screen_show(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Season Time");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 16);

    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_opa(back, LV_OPA_30, 0);
    lv_obj_t *back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(back_icon);
    lv_obj_add_event_cb(back, on_back_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *section = lv_label_create(scr);
    lv_label_set_text(section, "Daylight Saving Time");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_make(160, 160, 160), 0);
    lv_obj_align(section, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -20);

    s_winter_btn = lv_btn_create(row);
    lv_obj_set_size(s_winter_btn, 180, 60);
    lv_obj_add_event_cb(s_winter_btn, on_winter_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *winter_lbl = lv_label_create(s_winter_btn);
    lv_label_set_text(winter_lbl, "Winter Time");
    lv_obj_set_style_text_font(winter_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(winter_lbl);

    s_summer_btn = lv_btn_create(row);
    lv_obj_set_size(s_summer_btn, 180, 60);
    lv_obj_add_event_cb(s_summer_btn, on_summer_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *summer_lbl = lv_label_create(s_summer_btn);
    lv_label_set_text(summer_lbl, "Summer Time");
    lv_obj_set_style_text_font(summer_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(summer_lbl);

    refresh_dst_buttons();

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Summer time is 1 hour behind winter time");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(120, 120, 120), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);

    lv_disp_load_scr(scr);
}
