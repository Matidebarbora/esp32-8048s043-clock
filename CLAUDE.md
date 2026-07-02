# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & flash

```bash
# Build only
pio run

# Build and upload
pio run -t upload

# Open serial monitor (115200 baud, with exception decoder)
pio device monitor

# Build + upload + monitor in one step
pio run -t upload && pio device monitor
```

After changing `sdkconfig.defaults`, delete `.pio/build` before rebuilding so the sdkconfig is regenerated from scratch.

## Configuration

All user-facing settings live in `src/config.h`:

- **`WIFI_NETWORKS[]`** — array of `{ ssid, password }` structs tried in order until one connects.
- **`TIMEZONE`** — POSIX TZ string. For Argentina (UTC-3, no DST): `"<-03>3"`.
- **`NTP_SERVER`** — defaults to `"pool.ntp.org"`.

## Architecture

### Threading model

`LCDInit()` spawns an LVGL task pinned to **core 1** that calls `lv_tick_inc(20)` then `lv_task_handler()` every 20 ms.  `app_main` runs on **core 0**.  Any LVGL call from `app_main` must be wrapped with `lvgl_acquire()` / `lvgl_release()`, which take/give a mutex (`xGuiSemaphore`) checked by task-handle identity so the LVGL task can call LVGL without deadlock.

### Startup sequence

`app_main` runs strictly sequentially:
1. `LCDInit()` — RGB panel + GT911 touch (direct I²C) + LVGL init + LVGL task
2. Backlight on (`LCD_PIN_BK_LIGHT` HIGH)
3. `build_ui()` + `lv_timer_create(clock_tick_cb, 1000)` — creates clock labels, starts 1-second timer
4. `wifi_connect_any()` (10 s budget) — tries each network in `WIFI_NETWORKS`
5. `time_sync()` (15 s budget) — SNTP via `esp_netif_sntp_*` (IDF 5.x API)
6. On WiFi/NTP failure → time and date are simply left blank (no manual entry fallback; `clock_tick_cb()` no-ops while the system clock is unset)
7. `vTaskSuspend(NULL)` — clock updates driven by the LVGL timer, app_main is no longer needed

### LVGL configuration

LVGL is configured via Kconfig (`sdkconfig.defaults`) — `CONFIG_LV_CONF_SKIP=y` means there is **no** `lv_conf.h`. To enable additional fonts or widgets, add the corresponding `CONFIG_LV_*` line and delete `.pio/build`.

`lv_tick_inc(20)` **must** be called every LVGL task iteration — without it, LVGL timers and the indev poll never fire (display freezes, touch never reported).

Fonts currently enabled: Montserrat 14, 16, 24, 48.

### Dependencies

| Component | Version | Purpose |
|---|---|---|
| `lvgl/lvgl` | 8.3.11 | UI framework |

`espressif/esp_lcd_touch_gt911` was removed — GT911 is driven with direct I²C in `lcd.cpp`.

Downloaded to `managed_components/` on first build. Do not commit that directory.

### Flash layout (`partitions.csv`)

| Partition | Size |
|---|---|
| nvs | 20 KB |
| phy_init | 4 KB |
| app (factory) | 7 MB |
| storage (spiffs) | ~9 MB |

16 MB flash; `board_upload.flash_size = 16MB` in `platformio.ini`.

### Hardware pin map

All GPIO assignments are in `src/lcd.h`. Key ones:
- Backlight: GPIO 2 (active HIGH)
- Pixel clock: GPIO 42 — **14 MHz** (18 MHz only survives *static* screens, see note below)
- Touch I²C: SDA=19, SCL=20, INT=18, RST=38

## Critical hardware details — ESP32-8048S043

### RGB panel timing (lcd.cpp)

Working values (found empirically):

```cpp
.hsync_pulse_width = 4,  .hsync_back_porch = 100, .hsync_front_porch = 8,
.vsync_pulse_width = 4,  .vsync_back_porch = 12,  .vsync_front_porch = 8,
```

`hsync_back_porch = 100` is the key value. Lower values (8, 43) shift the image left; higher values shift it right. Adjust in increments of ~20 px if the image appears off-center after a hardware change.

**Non-deterministic horizontal offset per boot** is fixed by enabling the bounce buffer:

```cpp
.bounce_buffer_size_px = LCD_H_RES * 10,  // 16 KB internal SRAM
```

Without the bounce buffer, the RGB DMA can start at a random offset in the PSRAM framebuffer, causing the image to appear at a different horizontal position on each reboot.

`full_refresh = 1` is required when using double framebuffer in PSRAM; without it, the non-active framebuffer contains stale LVGL data that leaks onto the display.

**Pixel clock must have margin for heavy SW-render load, not just static content.** 18 MHz (`LCD_PIXEL_CLOCK_HZ`) looked "reliable" because it was only validated against static/light screens. It broke down under a widget-heavy screen (an on-screen `lv_keyboard` — 30+ buttons, full_refresh forces a complete 800×480 SW re-render on every keypress): the panel would visibly lose sync — **same image, shifted and clipped**, not a different screen's content — while the CPU was busy re-rendering + copying a full frame. This is a DMA/bus-bandwidth starvation symptom: heavy CPU-driven PSRAM traffic (the SW render + the `esp_lcd_panel_draw_bitmap` copy) leaves the bounce-buffer DMA refill too little bus time per scanline, so the RGB timing generator glitches.

Fix: lowered `LCD_PIXEL_CLOCK_HZ` from 18 MHz to **14 MHz**, giving the DMA more time margin per line. This fully resolved it. Things that were tried and did **not** help (don't re-attempt without new evidence):
- Disabling `CONFIG_LV_THEME_DEFAULT_TRANSITION_TIME` (button press animations) — no effect.
- Doubling `bounce_buffer_size_px` (16 KB → 32 KB) — no effect on its own.
- Making the LVGL SW draw buffer full-frame-sized instead of 100-row chunks — made it *worse*.
- Handing LVGL the panel's own framebuffers directly via `esp_lcd_rgb_panel_get_frame_buffer()` (zero-copy) — made it dramatically worse (even the once-a-second clock redraw started flickering). Do not retry this without deeply understanding this driver's swap semantics first.
- Lowering `LCD_PIXEL_CLOCK_HZ` further to 12 MHz — this doesn't just glitch under load, it breaks HSYNC/VSYNC sync entirely: the panel shows random-color garbage on every boot, not just during heavy redraw. **14 MHz is the floor for this panel** — do not go lower.

If a future screen reintroduces glitching under heavy redraw, suspect pixel-clock margin before touching buffer architecture again — but stay at or above 14 MHz.

### GT911 capacitive touch (lcd.cpp)

The `esp_lcd_touch_gt911` component was abandoned because its reset sequence was unreliable. Touch is driven directly via `i2c_master_write_read_device` / `i2c_master_write_to_device` (legacy ESP-IDF I²C driver, I2C_NUM_0).

**GT911 native resolution is 480×272** (not 800×480). Read from config registers 0x8048/0x804A. Coordinates must be scaled:

```cpp
x_lcd = raw_x * LCD_H_RES / 480   // 800/480
y_lcd = raw_y * LCD_V_RES / 272   // 480/272
```

**I²C address selection**: INT pin must be LOW during RST release → selects address 0x5D.

**Reset sequence** (must be exact):
```cpp
// INT LOW → selects I2C address 0x5D
gpio_set_level(TOUCH_PIN_INT,   0);
gpio_set_level(TOUCH_PIN_RESET, 0);
vTaskDelay(pdMS_TO_TICKS(10));
gpio_set_level(TOUCH_PIN_RESET, 1);
vTaskDelay(pdMS_TO_TICKS(100));  // GT911 needs ~55 ms minimum; 100 ms is safe
gpio_set_direction(TOUCH_PIN_INT, GPIO_MODE_INPUT);
gpio_set_pull_mode(TOUCH_PIN_INT, GPIO_PULLUP_ONLY);
```

**Status register protocol** (0x814E):
- Bit 7 = buffer ready; bits 3:0 = touch count
- Point 0 data starts at 0x814F (8 bytes: trackId, xL, xH, yL, yH, sL, sH, pad)
- Always write 0x00 to 0x814E after reading to let GT911 produce the next sample

**Touch callback must maintain last-known state.** When bit 7 of 0x814E is 0 (no new sample yet), the callback must report the previous state — NOT `RELEASED`. Reporting `RELEASED` when the finger is still down interrupts drag gestures (rollers, sliders stop responding):

```cpp
if (status & 0x80) {
    if (cnt >= 1) { update s_touch_x, s_touch_y, s_touch_state = PRESSED; }
    else          { s_touch_state = RELEASED; }
    gt911_i2c_write_byte(GT911_STATUS, 0);
}
data->point.x = s_touch_x;
data->point.y = s_touch_y;
data->state   = s_touch_state;  // keep last known when no new data
```

### LVGL UI notes

**Flex containers steal scroll gestures from rollers.** `lv_obj_create()` sets `LV_OBJ_FLAG_SCROLLABLE` by default. If a roller sits inside a scrollable container, LVGL routes the drag gesture to the container instead of the roller, so the roller never scrolls. Always call:

```cpp
lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
```

on any container that holds interactive widgets.

**Prefer +/− buttons over `lv_roller` for number input.** Even with the scrollable-flag fix, rollers can be unreliable for precise input on this board. Simple LVGL buttons with click callbacks are robust and easy to tap.

**`lv_timer_create` callbacks run inside `lv_task_handler()`** — no mutex needed. Callbacks invoked from other tasks (e.g., `app_main`) must use `lvgl_acquire()` / `lvgl_release()`.
