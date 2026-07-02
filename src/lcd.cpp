// LCD + touch initialisation for the ESP32-8048S043 (Sunton 800x480 RGB panel).

#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "lcd.h"

#define TAG "lcd"

// ── Backlight brightness (LEDC PWM) ─────────────────────────────────────────
#define BACKLIGHT_LEDC_TIMER   LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_FREQ_HZ 1000  // cheap backlight boost-driver ICs often cut out (instead of
                                     // dimming) at a few kHz; lower frequency is more reliable
#define BACKLIGHT_LEDC_RES     LEDC_TIMER_10_BIT  // duty range 0-1023

static bool s_backlight_pwm_ready = false;

static void backlight_pwm_init()
{
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer_cfg.timer_num       = BACKLIGHT_LEDC_TIMER;
    timer_cfg.duty_resolution = BACKLIGHT_LEDC_RES;
    timer_cfg.freq_hz         = BACKLIGHT_LEDC_FREQ_HZ;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.gpio_num   = LCD_PIN_BK_LIGHT;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = BACKLIGHT_LEDC_CHANNEL;
    ch_cfg.timer_sel  = BACKLIGHT_LEDC_TIMER;
    ch_cfg.duty       = 0;
    ch_cfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    s_backlight_pwm_ready = true;
}

extern "C" void lcd_set_backlight(uint8_t percent)
{
    if (!s_backlight_pwm_ready) backlight_pwm_init();
    if (percent > 100) percent = 100;

    uint32_t max_duty = (1 << BACKLIGHT_LEDC_RES) - 1;
    uint32_t duty      = (uint32_t)percent * max_duty / 100;
    if (!LCD_BK_LIGHT_ON_LEVEL) duty = max_duty - duty;  // active-low backlight

    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_LEDC_CHANNEL);
}

// ── GT911 direct I2C ─────────────────────────────────────────────────────────
// The esp_lcd_touch_gt911 abstraction layer never sets bit 7 of 0x814E reliably;
// we talk to the chip directly instead.
#define GT911_ADDR   0x5D
#define GT911_STATUS 0x814E   // bit7=data ready, bits3:0=touch count
#define GT911_POINT0 0x814F   // 8 bytes/point: trackId, xL, xH, yL, yH, sL, sH, pad

// Native resolution confirmed by reading chip config registers (0x8048/0x804A)
#define GT911_H_RES 480
#define GT911_V_RES 272

static esp_err_t gt911_i2c_read(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t reg_buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return i2c_master_write_read_device(I2C_NUM_0, GT911_ADDR,
                                        reg_buf, 2, buf, len,
                                        pdMS_TO_TICKS(10));
}

static esp_err_t gt911_i2c_write_byte(uint16_t reg, uint8_t val)
{
    uint8_t buf[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val};
    return i2c_master_write_to_device(I2C_NUM_0, GT911_ADDR, buf, 3,
                                      pdMS_TO_TICKS(10));
}

// ── LVGL plumbing ─────────────────────────────────────────────────────────────
static SemaphoreHandle_t xGuiSemaphore = NULL;
static TaskHandle_t g_lvgl_task_handle;

static void lcd_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                               lv_color_t *color_map)
{
    auto panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              color_map);
    lv_disp_flush_ready(drv);
}

static lv_indev_state_t s_touch_state = LV_INDEV_STATE_RELEASED;
static lv_coord_t       s_touch_x = 0, s_touch_y = 0;

static void gt911_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    uint8_t status = 0;
    gt911_i2c_read(GT911_STATUS, &status, 1);

    if (status & 0x80) {
        // GT911 has a fresh report — update state and clear flag
        uint8_t cnt = status & 0x0F;
        if (cnt >= 1 && cnt <= 5) {
            uint8_t pts[8] = {};
            gt911_i2c_read(GT911_POINT0, pts, 8);
            uint16_t x = (uint16_t)(pts[2] << 8) | pts[1];
            uint16_t y = (uint16_t)(pts[4] << 8) | pts[3];
            s_touch_x     = (lv_coord_t)((uint32_t)x * LCD_H_RES / GT911_H_RES);
            s_touch_y     = (lv_coord_t)((uint32_t)y * LCD_V_RES / GT911_V_RES);
            s_touch_state = LV_INDEV_STATE_PRESSED;
        } else {
            // cnt == 0: all fingers lifted
            s_touch_state = LV_INDEV_STATE_RELEASED;
        }
        gt911_i2c_write_byte(GT911_STATUS, 0);
    }
    // When bit 7 is 0 (GT911 hasn't produced a new sample yet), keep the last
    // known state so a drag gesture is never interrupted mid-movement.

    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    data->state   = s_touch_state;
}

extern "C" void lvgl_acquire(void)
{
    if (g_lvgl_task_handle != xTaskGetCurrentTaskHandle())
        xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
}

extern "C" void lvgl_release(void)
{
    if (g_lvgl_task_handle != xTaskGetCurrentTaskHandle())
        xSemaphoreGive(xGuiSemaphore);
}

static void lvgl_task(void *)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        lv_tick_inc(20);   // advance LVGL's internal clock — required for timers and indev polling
        if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
        }
    }
}

esp_err_t LCDInit(void)
{
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t      disp_drv;
    static lv_indev_drv_t     indev_drv;
    static esp_lcd_panel_handle_t panel = nullptr;

    // Backlight off until display is ready
    gpio_config_t bk_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BK_LIGHT,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&bk_cfg));
    gpio_set_level(LCD_PIN_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);

    // RGB panel
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res   = LCD_H_RES,
            .v_res   = LCD_V_RES,
            .hsync_pulse_width  = 4,  .hsync_back_porch  = 100, .hsync_front_porch = 8,
            .vsync_pulse_width  = 4,  .vsync_back_porch  = 12,  .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low  = false, .vsync_idle_low = false,
                .de_idle_high    = false, .pclk_active_neg = true,
                .pclk_idle_high  = false
            }
        },
        .data_width            = 16,
        .bits_per_pixel        = 0,
        .num_fbs               = 2,
        .bounce_buffer_size_px = LCD_H_RES * 20,  // 32 KB SRAM bounce buffer — more DMA runway under heavy SW-render PSRAM traffic
        .sram_trans_align      = 0,
        .psram_trans_align     = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC, .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num    = LCD_PIN_DE,    .pclk_gpio_num  = LCD_PIN_PCLK,
        .disp_gpio_num  = LCD_PIN_DISP_EN,
        .data_gpio_nums = {
            LCD_PIN_DATA0,  LCD_PIN_DATA1,  LCD_PIN_DATA2,  LCD_PIN_DATA3,
            LCD_PIN_DATA4,  LCD_PIN_DATA5,  LCD_PIN_DATA6,  LCD_PIN_DATA7,
            LCD_PIN_DATA8,  LCD_PIN_DATA9,  LCD_PIN_DATA10, LCD_PIN_DATA11,
            LCD_PIN_DATA12, LCD_PIN_DATA13, LCD_PIN_DATA14, LCD_PIN_DATA15
        },
        .flags = {
            .disp_active_low  = 0, .refresh_on_demand = 0,
            .fb_in_psram      = true, .double_fb       = true,
            .no_fb            = 0,   .bb_invalidate_cache = 0
        }
    };

    ESP_LOGI(TAG, "Install RGB panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    // ── GT911: I2C init + proper reset ───────────────────────────────────────
    {
        i2c_config_t conf = {
            .mode           = I2C_MODE_MASTER,
            .sda_io_num     = TOUCH_PIN_SDA,
            .scl_io_num     = TOUCH_PIN_SCL,
            .sda_pullup_en  = GPIO_PULLUP_ENABLE,
            .scl_pullup_en  = GPIO_PULLUP_ENABLE,
            .master = { .clk_speed = TOUCH_FREQ_HZ },
            .clk_flags = 0
        };
        ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
        ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

        // INT LOW during RST release → I2C address 0x5D; 100 ms boot time
        gpio_reset_pin(TOUCH_PIN_RESET);
        gpio_reset_pin(TOUCH_PIN_INT);
        gpio_set_direction(TOUCH_PIN_RESET, GPIO_MODE_OUTPUT);
        gpio_set_direction(TOUCH_PIN_INT,   GPIO_MODE_OUTPUT);
        gpio_set_level(TOUCH_PIN_INT,   0);
        gpio_set_level(TOUCH_PIN_RESET, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(TOUCH_PIN_RESET, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_direction(TOUCH_PIN_INT, GPIO_MODE_INPUT);
        gpio_set_pull_mode(TOUCH_PIN_INT, GPIO_PULLUP_ONLY);

        uint8_t id[3] = {};
        gt911_i2c_read(0x8140, id, 3);
        ESP_LOGI(TAG, "GT911 ID: %c%c%c", id[0], id[1], id[2]);
    }

    // ── LVGL init ─────────────────────────────────────────────────────────────
    ESP_LOGI(TAG, "Init LVGL");
    lv_init();

    // Two attempts to fix on-screen-keyboard flicker by changing this buffer
    // setup (panel's own framebuffers directly, then full-frame SW buffers)
    // each made the problem WORSE rather than better — a sign the real cause
    // is not here. Reverted to the original, smallest-footprint config that
    // was stable for the clock, time-entry, and network-list screens.
    void *buf1 = heap_caps_malloc(LCD_H_RES * 100 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(LCD_H_RES * 100 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1 && buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * 100);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = LCD_H_RES;
    disp_drv.ver_res      = LCD_V_RES;
    disp_drv.flush_cb     = lcd_lvgl_flush_cb;
    disp_drv.draw_buf     = &disp_buf;
    disp_drv.user_data    = panel;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_touchpad_read;
    if (!lv_indev_drv_register(&indev_drv))
        return ESP_FAIL;

    xGuiSemaphore = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr,
                            tskIDLE_PRIORITY + 1, &g_lvgl_task_handle, 1);
    return ESP_OK;
}
