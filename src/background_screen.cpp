#include "background_screen.h"
#include "lcd.h"
#include "background_settings.h"

static lv_obj_t *s_parent_scr = nullptr;
static lv_obj_t *s_list       = nullptr;

#define ROW_HEIGHT 64
#define SWATCH_W   56
#define SWATCH_H   40

static void rebuild_list();

static void on_back_click(lv_event_t *e)
{
    lv_obj_t *this_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_parent_scr);
    lv_obj_del_async(this_scr);
}

static void on_option_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    background_settings_set_index(idx);
    rebuild_list();  // refresh the checkmark
}

// One row: a swatch previewing the solid color (or gradient, same vertical
// direction as the actual screen background) + its name + a checkmark on
// whichever row is currently active.
static void build_option_row(lv_obj_t *parent, int idx, bool is_last)
{
    const background_option_t *opt       = &BACKGROUND_OPTIONS[idx];
    bool                       is_active = idx == background_settings_get_index();

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_PCT(100), ROW_HEIGHT);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, lv_color_make(36, 36, 42), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (!is_last) {
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_make(46, 46, 52), 0);
    }
    lv_obj_add_event_cb(row, on_option_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *swatch = lv_obj_create(row);
    lv_obj_remove_style_all(swatch);
    lv_obj_clear_flag(swatch, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(swatch, SWATCH_W, SWATCH_H);
    lv_obj_set_style_radius(swatch, 8, 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_set_style_border_color(swatch, lv_color_make(60, 60, 66), 0);
    lv_obj_set_style_bg_color(swatch, lv_color_make(opt->r, opt->g, opt->b), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    if (opt->has_gradient) {
        lv_obj_set_style_bg_grad_color(swatch, lv_color_make(opt->r2, opt->g2, opt->b2), 0);
        lv_obj_set_style_bg_grad_dir(swatch, LV_GRAD_DIR_VER, 0);
    }

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, opt->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_obj_set_flex_grow(name, 1);

    if (is_active) {
        lv_obj_t *check = lv_label_create(row);
        lv_label_set_text(check, LV_SYMBOL_OK);
        lv_obj_set_style_text_font(check, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(check, lv_color_make(60, 200, 100), 0);
    }
}

static void rebuild_list()
{
    lv_obj_clean(s_list);
    for (int i = 0; i < BACKGROUND_OPTION_COUNT; i++)
        build_option_row(s_list, i, i == BACKGROUND_OPTION_COUNT - 1);
}

void background_screen_show(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // don't compete with the list for scroll gestures
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Background");
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

    s_list = lv_obj_create(scr);
    lv_obj_set_size(s_list, LCD_H_RES - 40, LCD_V_RES - 100);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);

    rebuild_list();

    lv_disp_load_scr(scr);
}
