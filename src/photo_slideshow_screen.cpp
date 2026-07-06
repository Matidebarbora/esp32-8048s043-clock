#include "photo_slideshow_screen.h"
#include "lcd.h"
#include "sd_card.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "photo_slideshow";

#define MAX_PHOTOS 60
#define PHOTO_INTERVAL_MS (8 * 1000)  // time each photo stays on screen

static lv_obj_t   *s_clock_scr     = nullptr;
static lv_timer_t *s_advance_timer = nullptr;
static lv_timer_t *s_clock_timer   = nullptr;
static lv_obj_t   *s_img           = nullptr;
static lv_obj_t   *s_time_label    = nullptr;

// *.bin filenames found in SD_PHOTOS_DIR, sorted, capped at MAX_PHOTOS —
// generous enough for a slideshow without an unbounded directory scan.
static char   s_photo_names[MAX_PHOTOS][32];
static size_t s_photo_count = 0;
static size_t s_photo_index = 0;

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static size_t scan_photos(void)
{
    DIR *dir = opendir(SD_PHOTOS_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Can't open %s (no card, or no /photos folder on it)", SD_PHOTOS_DIR);
        return 0;
    }

    size_t count = 0;
    struct dirent *ent;
    while (count < MAX_PHOTOS && (ent = readdir(dir)) != nullptr) {
        size_t len = strlen(ent->d_name);
        // Case-insensitive: with FATFS long-filename support off (LFN_NONE,
        // the ESP-IDF default), an 8.3-compatible name like "0001.bin" comes
        // back as "0001.BIN" — readdir() upper-cases short names.
        if (len < 5 || strcasecmp(ent->d_name + len - 4, ".bin") != 0) continue;

        // Store lower-cased: LVGL's built-in image decoder does its own
        // *case-sensitive* check for a literal ".bin" extension
        // (lv_img_decoder.c: strcmp(lv_fs_get_ext(src), "bin")) — silently,
        // with no log line, so "0001.BIN" gets rejected with zero indication
        // why. FAT's own file lookup is case-insensitive either way, so
        // lower-casing here doesn't affect actually finding the file.
        char *name = s_photo_names[count];
        strlcpy(name, ent->d_name, sizeof(s_photo_names[count]));
        for (char *c = name; *c; c++) *c = (char)tolower((unsigned char)*c);
        count++;
    }
    closedir(dir);

    qsort(s_photo_names, count, sizeof(s_photo_names[0]), name_cmp);
    ESP_LOGI(TAG, "Found %u photo(s) in %s", (unsigned)count, SD_PHOTOS_DIR);
    return count;
}

// LVGL's FS_STDIO driver (letter 'S', see sdkconfig.defaults) maps "S:<path>"
// to CONFIG_LV_FS_STDIO_PATH + <path> — i.e. "S:/photos/x.bin" resolves to
// SD_PHOTOS_DIR "/x.bin" on the mounted card. lv_img_set_src() copies this
// string internally, so a stack buffer here is safe.
static void show_photo(size_t index)
{
    char path[64];
    snprintf(path, sizeof(path), "S:/photos/%s", s_photo_names[index]);
    lv_img_set_src(s_img, path);
}

static void on_advance_timer(lv_timer_t *)
{
    s_photo_index = (s_photo_index + 1) % s_photo_count;
    show_photo(s_photo_index);
}

// Runs inside lv_task_handler — no mutex needed, same as clock_tick_cb.
static void on_clock_timer(lv_timer_t *)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    if (ti.tm_year <= 70) return;  // system clock not set yet

    char buf[8];
    strftime(buf, sizeof(buf), "%H:%M", &ti);
    lv_label_set_text(s_time_label, buf);
}

static void on_tap_close(lv_event_t *e)
{
    lv_obj_t *scr = lv_obj_get_screen(lv_event_get_target(e));
    if (s_advance_timer) { lv_timer_del(s_advance_timer); s_advance_timer = nullptr; }
    if (s_clock_timer)   { lv_timer_del(s_clock_timer);   s_clock_timer   = nullptr; }
    lv_disp_load_scr(s_clock_scr);
    lv_obj_del_async(scr);
}

void photo_slideshow_screen_show(lv_obj_t *clock_scr)
{
    s_clock_scr = clock_scr;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, on_tap_close, LV_EVENT_CLICKED, nullptr);

    s_photo_count = sd_card_is_mounted() ? scan_photos() : 0;

    if (s_photo_count == 0) {
        lv_obj_t *msg = lv_label_create(scr);
        lv_label_set_text(msg,
            sd_card_is_mounted() ? "No photos found on the SD card.\nSee tools/convert_photos.py"
                                  : "No SD card detected.");
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(msg, lv_color_make(150, 150, 150), 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(msg);
        lv_disp_load_scr(scr);
        return;
    }

    s_img = lv_img_create(scr);
    lv_obj_set_pos(s_img, 0, 0);
    s_photo_index = 0;
    show_photo(s_photo_index);

    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_time_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_time_label, LV_OPA_50, 0);
    lv_obj_set_style_radius(s_time_label, 8, 0);
    lv_obj_set_style_pad_hor(s_time_label, 10, 0);
    lv_obj_set_style_pad_ver(s_time_label, 4, 0);
    lv_label_set_text(s_time_label, "--:--");
    lv_obj_align(s_time_label, LV_ALIGN_TOP_RIGHT, -16, 16);
    on_clock_timer(nullptr);  // paint the real time immediately, don't wait a full second

    s_advance_timer = lv_timer_create(on_advance_timer, PHOTO_INTERVAL_MS, nullptr);
    s_clock_timer   = lv_timer_create(on_clock_timer, 1000, nullptr);

    lv_disp_load_scr(scr);
}
