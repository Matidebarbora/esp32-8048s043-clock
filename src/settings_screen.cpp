#include "settings_screen.h"
#include "lcd.h"
#include "season_time_screen.h"
#include "dimmer_screen.h"

static lv_obj_t *s_clock_scr = nullptr;

static void on_home_click(lv_event_t *e)
{
    lv_obj_t *settings_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_clock_scr);
    lv_obj_del_async(settings_scr);
}

static void on_season_time_click(lv_event_t *e)
{
    lv_obj_t *settings_scr = lv_obj_get_screen(lv_event_get_target(e));
    season_time_screen_show(settings_scr);
}

static void on_dimmer_click(lv_event_t *e)
{
    lv_obj_t *settings_scr = lv_obj_get_screen(lv_event_get_target(e));
    dimmer_screen_show(settings_scr);
}

void settings_screen_show(lv_obj_t *clock_scr)
{
    s_clock_scr = clock_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
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
    lv_obj_add_event_cb(home, on_home_click, LV_EVENT_CLICKED, nullptr);

    // Menu of settings options.
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LCD_H_RES - 40, LCD_V_RES - 100);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -14);

    lv_obj_t *season_btn = lv_list_add_btn(list, nullptr, "Season Time");
    lv_obj_set_style_text_font(season_btn, &lv_font_montserrat_28, 0);  // 2x the default 14px list item font
    lv_obj_add_event_cb(season_btn, on_season_time_click, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *dimmer_btn = lv_list_add_btn(list, nullptr, "Dimmer");
    lv_obj_set_style_text_font(dimmer_btn, &lv_font_montserrat_28, 0);
    lv_obj_add_event_cb(dimmer_btn, on_dimmer_click, LV_EVENT_CLICKED, nullptr);

    lv_disp_load_scr(scr);
}
