#include "dimmer_screen.h"
#include "lcd.h"
#include "dimmer_settings.h"

#include <stdio.h>

static lv_obj_t *s_parent_scr = nullptr;

static lv_obj_t *s_off_btn  = nullptr;
static lv_obj_t *s_on_btn   = nullptr;
static lv_obj_t *s_auto_btn = nullptr;

static const lv_color_t COLOR_ACTIVE   = LV_COLOR_MAKE(50, 120, 200);
static const lv_color_t COLOR_INACTIVE = LV_COLOR_MAKE(60, 60, 60);

// ── HH/MM steppers for the AUTO schedule ─────────────────────────────────────

struct Spinner {
    int value, vmax;
    lv_obj_t *lbl;
};

static Spinner sp_start_h, sp_start_m, sp_end_h, sp_end_m;

static void sp_update(Spinner *sp)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%02d", sp->value);
    lv_label_set_text(sp->lbl, buf);
}

static void persist_schedule()
{
    dimmer_settings_set_schedule(sp_start_h.value, sp_start_m.value, sp_end_h.value, sp_end_m.value);
}

static void sp_dec(lv_event_t *e)
{
    auto *sp = (Spinner *)lv_event_get_user_data(e);
    sp->value = (sp->value <= 0) ? sp->vmax : sp->value - 1;
    sp_update(sp);
    persist_schedule();
}

static void sp_inc(lv_event_t *e)
{
    auto *sp = (Spinner *)lv_event_get_user_data(e);
    sp->value = (sp->value >= sp->vmax) ? 0 : sp->value + 1;
    sp_update(sp);
    persist_schedule();
}

static void make_stepper(lv_obj_t *parent, Spinner *sp)
{
    lv_obj_t *bm = lv_btn_create(parent);
    lv_obj_set_size(bm, 36, 36);
    lv_obj_t *lm = lv_label_create(bm);
    lv_label_set_text(lm, "-");
    lv_obj_center(lm);
    lv_obj_add_event_cb(bm, sp_dec, LV_EVENT_CLICKED, sp);

    sp->lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(sp->lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sp->lbl, lv_color_white(), 0);
    lv_obj_set_width(sp->lbl, 40);
    lv_obj_set_style_text_align(sp->lbl, LV_TEXT_ALIGN_CENTER, 0);
    sp_update(sp);

    lv_obj_t *bp = lv_btn_create(parent);
    lv_obj_set_size(bp, 36, 36);
    lv_obj_t *lp = lv_label_create(bp);
    lv_label_set_text(lp, "+");
    lv_obj_center(lp);
    lv_obj_add_event_cb(bp, sp_inc, LV_EVENT_CLICKED, sp);
}

static lv_obj_t *make_schedule_row(lv_obj_t *parent, const char *caption, Spinner *sp_h, Spinner *sp_m)
{
    lv_obj_t *outer = lv_obj_create(parent);
    lv_obj_remove_style_all(outer);
    lv_obj_clear_flag(outer, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(outer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(outer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(outer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(outer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(outer, 8, 0);

    lv_obj_t *cap = lv_label_create(outer);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cap, lv_color_make(160, 160, 160), 0);
    lv_obj_set_width(cap, 110);

    make_stepper(outer, sp_h);

    lv_obj_t *colon = lv_label_create(outer);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(colon, lv_color_white(), 0);

    make_stepper(outer, sp_m);

    return outer;
}

// ── Mode buttons ──────────────────────────────────────────────────────────────

static void refresh_mode_buttons()
{
    dimmer_mode_t mode = dimmer_settings_get().mode;
    lv_obj_set_style_bg_color(s_off_btn,  mode == DIMMER_OFF  ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
    lv_obj_set_style_bg_color(s_on_btn,   mode == DIMMER_ON   ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
    lv_obj_set_style_bg_color(s_auto_btn, mode == DIMMER_AUTO ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
}

static void on_off_click(lv_event_t *)
{
    dimmer_settings_set_mode(DIMMER_OFF);
    refresh_mode_buttons();
}

static void on_on_click(lv_event_t *)
{
    dimmer_settings_set_mode(DIMMER_ON);
    refresh_mode_buttons();
}

static void on_auto_click(lv_event_t *)
{
    dimmer_settings_set_mode(DIMMER_AUTO);
    refresh_mode_buttons();
}

static void on_back_click(lv_event_t *e)
{
    lv_obj_t *this_scr = lv_obj_get_screen(lv_event_get_target(e));
    lv_disp_load_scr(s_parent_scr);
    lv_obj_del_async(this_scr);
}

void dimmer_screen_show(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Dimmer");
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

    // Mode row: OFF / ON / AUTO
    lv_obj_t *mode_row = lv_obj_create(scr);
    lv_obj_remove_style_all(mode_row);
    lv_obj_clear_flag(mode_row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(mode_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(mode_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(mode_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mode_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mode_row, 14, 0);
    lv_obj_align(mode_row, LV_ALIGN_CENTER, 0, -70);

    s_off_btn = lv_btn_create(mode_row);
    lv_obj_set_size(s_off_btn, 130, 56);
    lv_obj_add_event_cb(s_off_btn, on_off_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *off_lbl = lv_label_create(s_off_btn);
    lv_label_set_text(off_lbl, "OFF");
    lv_obj_set_style_text_font(off_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(off_lbl);

    s_on_btn = lv_btn_create(mode_row);
    lv_obj_set_size(s_on_btn, 130, 56);
    lv_obj_add_event_cb(s_on_btn, on_on_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *on_lbl = lv_label_create(s_on_btn);
    lv_label_set_text(on_lbl, "ON");
    lv_obj_set_style_text_font(on_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(on_lbl);

    s_auto_btn = lv_btn_create(mode_row);
    lv_obj_set_size(s_auto_btn, 130, 56);
    lv_obj_add_event_cb(s_auto_btn, on_auto_click, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *auto_lbl = lv_label_create(s_auto_btn);
    lv_label_set_text(auto_lbl, "AUTO");
    lv_obj_set_style_text_font(auto_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(auto_lbl);

    refresh_mode_buttons();

    // AUTO schedule — always editable, only applied while mode == AUTO
    dimmer_settings_t s = dimmer_settings_get();
    sp_start_h = { s.dim_start_hour, 23 };
    sp_start_m = { s.dim_start_min,  59 };
    sp_end_h   = { s.dim_end_hour,   23 };
    sp_end_m   = { s.dim_end_min,    59 };

    lv_obj_t *schedule_col = lv_obj_create(scr);
    lv_obj_remove_style_all(schedule_col);
    lv_obj_clear_flag(schedule_col, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(schedule_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(schedule_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(schedule_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(schedule_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(schedule_col, 14, 0);
    lv_obj_align(schedule_col, LV_ALIGN_CENTER, 0, 30);

    make_schedule_row(schedule_col, "Dim from",    &sp_start_h, &sp_start_m);
    make_schedule_row(schedule_col, "Bright from", &sp_end_h,   &sp_end_m);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "Schedule only applies in AUTO mode");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(120, 120, 120), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_disp_load_scr(scr);
}
