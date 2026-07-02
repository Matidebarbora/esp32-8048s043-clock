#include "wifi_settings.h"
#include "lcd.h"
#include "wifi_time.h"
#include "wifi_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_settings";

static lv_obj_t *s_clock_scr = nullptr;

// Points at the currently-shown network list, or NULL once that screen has
// been torn down. Touched only while holding xGuiSemaphore (either because
// we're inside an LVGL event callback, or via lvgl_acquire()), so the scan
// task and the home button never race on it.
static lv_obj_t *s_active_list = nullptr;

// ── Network list screen ──────────────────────────────────────────────────────

static void free_user_data_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static void on_network_click(lv_event_t *e);  // defined in the password-screen section below

static void on_home_click(lv_event_t *e)
{
    lv_obj_t *settings_scr = lv_obj_get_screen(lv_event_get_target(e));

    s_active_list = nullptr;  // tell any in-flight scan task to stop touching the list
    lv_disp_load_scr(s_clock_scr);
    lv_obj_del_async(settings_scr);
}

static void wifi_scan_task(void *)
{
    wifi_ap_info_t *aps   = nullptr;
    uint16_t        count = 0;
    esp_err_t       ret   = wifi_scan(&aps, &count);

    lvgl_acquire();
    if (s_active_list) {
        lv_obj_clean(s_active_list);
        if (ret != ESP_OK) {
            lv_list_add_text(s_active_list, "Error scanning networks");
        } else if (count == 0) {
            lv_list_add_text(s_active_list, "No networks found");
        } else {
            for (uint16_t i = 0; i < count; i++) {
                char label[33 + 24];
                snprintf(label, sizeof(label), "%s  (%d dBm)%s",
                        aps[i].ssid, aps[i].rssi, aps[i].secure ? "" : "  [open]");

                char *ssid_copy = strdup(aps[i].ssid);
                lv_obj_t *btn = lv_list_add_btn(s_active_list, LV_SYMBOL_WIFI, label);
                if (wifi_store_is_known(aps[i].ssid)) {
                    lv_obj_t *check = lv_label_create(btn);
                    lv_label_set_text(check, LV_SYMBOL_OK);
                    lv_obj_set_style_text_color(check, lv_color_make(60, 200, 100), 0);
                }
                lv_obj_add_event_cb(btn, on_network_click, LV_EVENT_CLICKED, ssid_copy);
                lv_obj_add_event_cb(btn, free_user_data_cb, LV_EVENT_DELETE, ssid_copy);
            }
        }
    }
    lvgl_release();

    free(aps);
    vTaskDelete(nullptr);
}

void wifi_settings_show(lv_obj_t *clock_scr)
{
    s_clock_scr = clock_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // don't compete with the list for scroll gestures
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Wi-Fi Networks");
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

    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, LCD_H_RES - 40, LCD_V_RES - 100);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_list_add_text(list, "Scanning networks...");

    s_active_list = list;

    lv_disp_load_scr(scr);

    BaseType_t ok = xTaskCreate(wifi_scan_task, "wifi_scan", 6144, nullptr,
                                tskIDLE_PRIORITY + 1, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn scan task");
        lv_obj_clean(list);
        lv_list_add_text(list, "Error scanning networks");
    }
}

// ── Password entry screen ────────────────────────────────────────────────────

struct ConnectUi {
    char      ssid[33];
    lv_obj_t *pw_scr;
    lv_obj_t *list_scr;
    lv_obj_t *password_ta;
    lv_obj_t *pwd_eye_icon;
    lv_obj_t *status_label;
    lv_obj_t *connect_btn;
};

// Valid only while a password screen is on display; NULL'd out the moment
// it's torn down (cancel, or connect success) so the connect task can tell
// whether its UI handles are still alive.
static ConnectUi *s_connect_ui = nullptr;

static void do_cancel(ConnectUi *ui)
{
    if (s_connect_ui != ui) return;  // already torn down
    s_connect_ui = nullptr;
    lv_disp_load_scr(ui->list_scr);
    lv_obj_del_async(ui->pw_scr);
    free(ui);
}

struct ConnectArgs {
    char ssid[33];
    char password[65];
};

static void connect_task(void *arg)
{
    auto *args = (ConnectArgs *)arg;
    esp_err_t ret = wifi_connect_one(args->ssid, args->password, 15000);

    if (ret == ESP_OK) wifi_store_save(args->ssid, args->password);

    lvgl_acquire();
    if (s_connect_ui) {
        if (ret == ESP_OK) {
            lv_label_set_text(s_connect_ui->status_label, "Connected!");
        } else {
            lv_label_set_text(s_connect_ui->status_label, "Could not connect");
            lv_obj_clear_state(s_connect_ui->connect_btn, LV_STATE_DISABLED);
        }
    }
    lvgl_release();

    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(900));
        lvgl_acquire();
        if (s_connect_ui) {
            ConnectUi *ui = s_connect_ui;
            s_connect_ui  = nullptr;
            s_active_list = nullptr;
            lv_disp_load_scr(s_clock_scr);
            lv_obj_del_async(ui->pw_scr);
            lv_obj_del_async(ui->list_scr);
            free(ui);
        }
        lvgl_release();
    }

    free(args);
    vTaskDelete(nullptr);
}

static void do_connect(ConnectUi *ui)
{
    if (s_connect_ui != ui) return;

    lv_obj_add_state(ui->connect_btn, LV_STATE_DISABLED);
    lv_label_set_text(ui->status_label, "Connecting...");

    auto *args = (ConnectArgs *)malloc(sizeof(ConnectArgs));
    strlcpy(args->ssid, ui->ssid, sizeof(args->ssid));
    strlcpy(args->password, lv_textarea_get_text(ui->password_ta), sizeof(args->password));

    BaseType_t ok = xTaskCreate(connect_task, "wifi_conn", 6144, args,
                                tskIDLE_PRIORITY + 1, nullptr);
    if (ok != pdPASS) {
        lv_label_set_text(ui->status_label, "Internal error");
        lv_obj_clear_state(ui->connect_btn, LV_STATE_DISABLED);
        free(args);
    }
}

static void on_back_click(lv_event_t *e)
{
    do_cancel((ConnectUi *)lv_event_get_user_data(e));
}

static void on_connect_click(lv_event_t *e)
{
    do_connect((ConnectUi *)lv_event_get_user_data(e));
}

static void on_ta_event(lv_event_t *e)
{
    auto *ui   = (ConnectUi *)lv_event_get_user_data(e);
    auto  code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        do_connect(ui);
    } else if (code == LV_EVENT_CANCEL) {
        do_cancel(ui);
    }
}

static void on_toggle_pwd_visibility(lv_event_t *e)
{
    auto *ui = (ConnectUi *)lv_event_get_user_data(e);
    bool was_hidden = lv_textarea_get_password_mode(ui->password_ta);
    lv_textarea_set_password_mode(ui->password_ta, !was_hidden);
    lv_label_set_text(ui->pwd_eye_icon, was_hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void build_password_screen(const char *ssid, lv_obj_t *list_scr)
{
    auto *ui = (ConnectUi *)malloc(sizeof(ConnectUi));
    strlcpy(ui->ssid, ssid, sizeof(ui->ssid));
    ui->list_scr = list_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // don't drift/scroll under keyboard taps
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui->pw_scr = scr;

    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_opa(back, LV_OPA_30, 0);
    lv_obj_t *back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(back_icon);
    lv_obj_add_event_cb(back, on_back_click, LV_EVENT_CLICKED, ui);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "Password");
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_size(ta, 380, 68);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, -35, 76);
    lv_obj_add_event_cb(ta, on_ta_event, LV_EVENT_ALL, ui);
    ui->password_ta = ta;

    lv_obj_t *eye = lv_btn_create(scr);
    lv_obj_set_size(eye, 60, 68);
    lv_obj_align_to(eye, ta, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_30, 0);
    lv_obj_t *eye_icon = lv_label_create(eye);
    lv_label_set_text(eye_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(eye_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(eye_icon);
    lv_obj_add_event_cb(eye, on_toggle_pwd_visibility, LV_EVENT_CLICKED, ui);
    ui->pwd_eye_icon = eye_icon;

    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status, lv_color_make(160, 160, 160), 0);
    lv_obj_align_to(status, ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    ui->status_label = status;

    lv_obj_t *connect = lv_btn_create(scr);
    lv_obj_set_size(connect, 150, 44);
    lv_obj_align_to(connect, status, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_t *connect_lbl = lv_label_create(connect);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_set_style_text_font(connect_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(connect_lbl);
    lv_obj_add_event_cb(connect, on_connect_click, LV_EVENT_CLICKED, ui);
    ui->connect_btn = connect;

    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_size(kb, LCD_H_RES, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Flatten button styling: every keypress on this screen forces a full
    // 800x480 software re-render (full_refresh is required to avoid stale
    // double-buffer ghosting on this RGB panel). Shadows/transitions/pressed
    // recoloring each add another full-screen redraw pass per tap, which on
    // a 30+ button keyboard was visibly janky. Cut it down to one redraw.
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    lv_color_t key_bg = lv_color_make(55, 55, 55);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(kb, key_bg, LV_PART_ITEMS | LV_STATE_CHECKED);

    s_connect_ui = ui;
    lv_disp_load_scr(scr);
}

// ── Saved-network options screen (tapping a network with a green check) ─────

struct ForgetUi {
    char      ssid[33];
    lv_obj_t *scr;
    lv_obj_t *list_scr;
};

// Valid only while the options screen is on display; NULL'd out the moment
// it's torn down, same pattern as s_connect_ui.
static ForgetUi *s_forget_ui = nullptr;

static void on_forget_back_click(lv_event_t *e)
{
    auto *ui = (ForgetUi *)lv_event_get_user_data(e);
    if (s_forget_ui != ui) return;
    s_forget_ui = nullptr;
    lv_disp_load_scr(ui->list_scr);
    lv_obj_del_async(ui->scr);
    free(ui);
}

static void on_forget_confirm_click(lv_event_t *e)
{
    auto *ui = (ForgetUi *)lv_event_get_user_data(e);
    if (s_forget_ui != ui) return;

    wifi_store_forget(ui->ssid);

    s_forget_ui   = nullptr;
    s_active_list = nullptr;
    lv_obj_del_async(ui->scr);
    lv_obj_del_async(ui->list_scr);
    free(ui);

    // Re-open the networks screen fresh so the list (and its check marks)
    // reflects the updated saved state.
    wifi_settings_show(s_clock_scr);
}

static void build_network_options_screen(const char *ssid, lv_obj_t *list_scr)
{
    auto *ui = (ForgetUi *)malloc(sizeof(ForgetUi));
    strlcpy(ui->ssid, ssid, sizeof(ui->ssid));
    ui->list_scr = list_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui->scr = scr;

    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 44, 44);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_set_style_bg_opa(back, LV_OPA_30, 0);
    lv_obj_t *back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_24, 0);
    lv_obj_center(back_icon);
    lv_obj_add_event_cb(back, on_forget_back_click, LV_EVENT_CLICKED, ui);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, ssid);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *saved_label = lv_label_create(scr);
    lv_label_set_text(saved_label, LV_SYMBOL_OK "  Saved network");
    lv_obj_set_style_text_font(saved_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(saved_label, lv_color_make(60, 200, 100), 0);
    lv_obj_align(saved_label, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *forget = lv_btn_create(scr);
    lv_obj_set_size(forget, 280, 52);
    lv_obj_align(forget, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(forget, lv_color_make(150, 50, 50), 0);
    lv_obj_add_event_cb(forget, on_forget_confirm_click, LV_EVENT_CLICKED, ui);
    lv_obj_t *forget_lbl = lv_label_create(forget);
    lv_label_set_text(forget_lbl, LV_SYMBOL_TRASH "  Forget this network");
    lv_obj_set_style_text_font(forget_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(forget_lbl);

    s_forget_ui = ui;
    lv_disp_load_scr(scr);
}

static void on_network_click(lv_event_t *e)
{
    char     *ssid     = (char *)lv_event_get_user_data(e);
    lv_obj_t *list_scr = lv_obj_get_screen(lv_event_get_target(e));
    if (wifi_store_is_known(ssid)) {
        build_network_options_screen(ssid, list_scr);
    } else {
        build_password_screen(ssid, list_scr);
    }
}
