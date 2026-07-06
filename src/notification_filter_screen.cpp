#include "notification_filter_screen.h"
#include "lcd.h"
#include "notification_filter_settings.h"

static lv_obj_t *s_parent_scr = nullptr;

static void on_back_click(lv_event_t *e)
{
    lv_obj_t *this_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_parent_scr);
    lv_obj_del_async(this_scr);
}

static void on_whatsapp_toggle(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    notification_filter_set_hide_whatsapp(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

// One "App name ... [switch]" row. Add more calls to this for future filters.
static void build_toggle_row(lv_obj_t *parent, const char *label_text, bool initial_checked, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 24, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_style_bg_color(sw, lv_color_make(50, 120, 200), (lv_style_selector_t)(LV_PART_INDICATOR | LV_STATE_CHECKED));
    if (initial_checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
}

void notification_filter_screen_show(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Notifications");
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
    lv_label_set_text(section, "Hide notifications from");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_make(160, 160, 160), 0);
    lv_obj_align(section, LV_ALIGN_TOP_LEFT, 24, 74);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(list, lv_color_make(26, 26, 30), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 20, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_border_color(list, lv_color_make(46, 46, 52), 0);
    lv_obj_set_size(list, LCD_H_RES - 48, LV_SIZE_CONTENT);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 108);

    build_toggle_row(list, "WhatsApp", notification_filter_get_hide_whatsapp(), on_whatsapp_toggle);

    lv_disp_load_scr(scr);
}
