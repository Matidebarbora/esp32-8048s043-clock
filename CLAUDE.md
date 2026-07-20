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

`src/config.h`:

- **`WIFI_NETWORKS[]`** — array of `{ ssid, password }` structs tried in order until one connects. Networks saved later from the on-device WiFi settings screen (`wifi_store.cpp`, NVS-backed) are merged in at runtime via `build_connect_candidates()` in `main.cpp` — `config.h` only seeds the initial/default list.
- **`NTP_SERVER`** — defaults to `"pool.ntp.org"`.
- **`FALLBACK_LATITUDE` / `FALLBACK_LONGITUDE`** — used for the weather card only if IP geolocation (`geolocation.cpp`) fails; normally location is auto-detected.

Time zone (winter/summer DST) is **not** in `config.h` — it's chosen at runtime via the Settings screen and persisted to NVS by `app_settings.cpp`.

## File structure

| File(s) | Purpose |
|---|---|
| `main.cpp` | `app_main()`, `build_ui()` (the whole clock screen), background tasks (`weather_task`, `wifi_reconnect_task`, `dimmer_task`), the sun-path arc math |
| `lcd.cpp` / `lcd.h` | RGB panel + GT911 touch init, LVGL task, `lvgl_acquire()`/`lvgl_release()`, PWM backlight (`lcd_set_backlight()`) |
| `wifi_time.cpp` | `wifi_connect_any()` / `wifi_connect_one()` (mutex-serialized against each other), SNTP `time_sync()` |
| `wifi_settings.cpp` | WiFi settings screen: scan, connect (with on-screen keyboard password entry), forget saved networks |
| `wifi_store.cpp` | NVS-backed saved-network list consumed by `wifi_settings.cpp` and merged into connect candidates by `main.cpp` |
| `app_settings.cpp` | NVS-backed app settings — currently just DST mode (winter/summer), sets the POSIX `TZ` env var |
| `settings_screen.cpp` | Settings menu (gear icon) — list of sub-screens |
| `season_time_screen.cpp` | Settings sub-screen: winter/summer DST toggle |
| `dimmer_settings.cpp` / `dimmer_screen.cpp` | NVS-backed backlight dimmer (OFF/ON/AUTO with scheduled start/end times) + its settings sub-screen |
| `background_settings.cpp` / `background_screen.cpp` | NVS-backed screen background choice (`BACKGROUND_OPTIONS[]` — solid colors and top-to-bottom gradients, index 0 always black) + its settings sub-screen (scrollable list of swatches). Unlike the other `_settings` modules, `background_settings_init()` must run *before* `build_ui()` — see "Startup sequence" below |
| `weather.cpp` / `weather_icon.cpp` / `weather_icons_data.c` | Open-Meteo fetch/parse + cache, WMO-code-to-icon classification, Twemoji-derived icon bitmaps (sun/moon/cloud/rain/snow/storm, 100px + 32px) |
| `geolocation.cpp` | One-time IP-based geolocation (ip-api.com) for the weather card's coordinates |
| `forecast_screen.cpp` | 5-day forecast screen (tap the weather card) |
| `time_digits_data.c` / `bulb_icon_data.c` | Generated image assets — big clock digits, brightness bulb icon |
| `stocks.cpp` / `stocks_store.cpp` / `stocks_screen.cpp` | Yahoo Finance search/quote fetch (no API key — unofficial endpoints), NVS-backed pinned-symbol list (max 3), and the Settings sub-screen to search/pin/unpin — see "Stock tracking" below |
| `sd_card.cpp` | Mounts the microSD card (FATFS over SPI) at boot for the photo slideshow — see "Photo slideshow" below |
| `photo_slideshow_screen.cpp` | Full-screen photo slideshow (tap the time card) reading pre-converted images off the SD card, current time overlaid top-right, tap anywhere to exit |
| `tools/convert_photos.py` | Offline tool (run on a PC, not on-device) that converts photos to the raw LVGL-binary format `photo_slideshow_screen.cpp` reads — see "Photo slideshow" below |

## Architecture

### Threading model

`LCDInit()` spawns an LVGL task pinned to **core 1** that calls `lv_tick_inc(20)` then `lv_task_handler()` every 20 ms.  `app_main` runs on **core 0**.  Any LVGL call from `app_main` must be wrapped with `lvgl_acquire()` / `lvgl_release()`, which take/give a mutex (`xGuiSemaphore`) checked by task-handle identity so the LVGL task can call LVGL without deadlock. The same rule applies to every other task that touches the UI: `weather_task`, `wifi_reconnect_task`, `dimmer_task`.

### Startup sequence

`app_main` runs strictly sequentially:
1. `nvs_flash_init()` (with erase-and-retry on version mismatch). Safe to call again later — `wifi_store`/`app_settings`/`dimmer_settings`/`background_settings` each call it too; it's idempotent.
2. `LCDInit()` — RGB panel + GT911 touch (direct I²C) + LVGL init + LVGL task; backlight on
3. `background_settings_init()` + change-callback — must happen before `build_ui()`, unlike every other `_settings` module below, since `build_ui()` reads it once to paint the screen's initial background
4. `build_ui()` + `clock_tick_cb` (1 s) + `wifi_status_tick_cb` (2 s) timers
5. `app_settings_init()` — loads DST mode, sets `TZ` (must happen before any time read)
6. `wifi_store_init()` + `build_connect_candidates()`, `wifi_time_init()` (creates the connect mutex — must happen before any task that might call `wifi_connect_any()`/`wifi_connect_one()` is spawned)
7. `dimmer_settings_init()` + change-callback
8. Spawn `weather_task`, `wifi_reconnect_task`, `dimmer_task`
9. `wifi_connect_any()` (10 s budget) → on success, `time_sync()` (SNTP). On WiFi/NTP failure, time and date are simply left blank — no manual entry fallback; `clock_tick_cb()` no-ops while the system clock is unset (`tm_year <= 70`)
10. `vTaskSuspend(NULL)` — everything from here on is timer/task driven, `app_main` is no longer needed

### LVGL configuration

LVGL is configured via Kconfig (`sdkconfig.defaults`) — `CONFIG_LV_CONF_SKIP=y` means there is **no** `lv_conf.h`. To enable additional fonts or widgets, add the corresponding `CONFIG_LV_*` line and delete `.pio/build` (see the sdkconfig-regeneration gotcha below — this applies to *any* new Kconfig option, not just fonts).

`lv_tick_inc(20)` **must** be called every LVGL task iteration — without it, LVGL timers and the indev poll never fire (display freezes, touch never reported).

Fonts currently enabled: Montserrat 14, 16, 20, 24, 28, 32, 48.

**Editing `sdkconfig.defaults` alone is not enough.** The persisted `sdkconfig.esp32s3-clock` file does **not** auto-regenerate new options from `sdkconfig.defaults` once it already exists — it only fills in options that weren't set before. After adding/changing anything in `sdkconfig.defaults`, delete **both** `.pio/build` **and** `sdkconfig.esp32s3-clock`, then rebuild. This has bitten every Kconfig change in this project (fonts, then the SPIRAM/mbedtls memory options) — check for it first whenever a new `CONFIG_*` setting doesn't seem to take effect.

**LVGL's own memory pool (`CONFIG_LV_MEM_SIZE_KILOBYTES`) is separate from the general heap/PSRAM** — every `lv_obj_t`, style, and internal render buffer comes out of this one pool, allocated once at `lv_init()`. The 32 KB default was fine for a simple clock face, but this UI grew enough (stocks screen, stock card, photo slideshow, the 5-day forecast screen's ~50 objects built at once) to start hitting `lv_mem_realloc: couldn't allocate memory` / `Out of memory, can't allocate a new buffer` opening the forecast screen.

- **Raising `CONFIG_LV_MEM_SIZE_KILOBYTES` to its Kconfig-enforced ceiling (128) bricked the board on boot** — `esp_lcd_new_rgb_panel()` failed with `ESP_ERR_NO_MEM` (`no mem for bounce buffer`), because by default this pool is a *static array* that lands in internal RAM, and 128 KB of it left too little contiguous internal RAM for the RGB panel's own bounce buffer. Screen never lights, boot-loops forever. If this happens, the fix is a re-flash with a corrected build — the board isn't actually bricked, just running a bad image.
- **The real fix**: keep `LV_MEM_SIZE_KILOBYTES=128` (still capped at 128 by Kconfig's `range 2 128`, and still not enough on its own) but source the pool's backing memory from PSRAM instead of that static array, via `LV_MEM_POOL_ALLOC` — a function-like macro LVGL's `lv_mem_init()` calls once to get the pool, *not* expressible through Kconfig (it needs a real macro, not a string), so it's set in **`platformio.ini`'s `build_flags`** instead: `-DLV_MEM_POOL_INCLUDE=\"esp_heap_caps.h\"` + `-DLV_MEM_POOL_ALLOC(size)=heap_caps_malloc(size,MALLOC_CAP_SPIRAM)`. This keeps LVGL's fast internal TLSF allocator for every small object (unlike `CONFIG_LV_MEM_CUSTOM`, which would route every allocation through the slower, more fragmentation-prone general heap) while pulling the one big backing block from PSRAM. Net effect: static RAM usage actually *dropped* below its pre-128KB baseline (the pool moved from a compile-time array to a runtime PSRAM allocation), with 4x the pool space LVGL has to work with.
- **Don't put a `|` (e.g. `MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT`) in a `platformio.ini` `build_flags` value on Windows** — cmd.exe treats it as a pipe and silently truncates the compiler command there, breaking the entire build with unrelated-looking "not recognized as an internal or external command" errors far away from the actual cause. `MALLOC_CAP_SPIRAM` alone is sufficient here; if a capability needs to be OR'd in again later, escape it or avoid the raw pipe character.
- **`esp_heap_caps.h` needed no extra `INCLUDE_DIRS` wiring** to be visible from `lv_mem.c` (which lives in the vendored `lvgl` component, not this project's own "main" component) — `build_flags` set in `platformio.ini` apply to PlatformIO's *entire* ESP-IDF build, across every component, not just this project's own files. A project-local header used the same way would need the same global visibility considered before assuming `#include` will resolve.

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

### Weather (weather.cpp, geolocation.cpp)

Open-Meteo (`api.open-meteo.com`) needs no API key but also has no built-in IP geolocation — coordinates come from a one-time `ip-api.com` lookup at boot (`geolocation.cpp`), falling back to `FALLBACK_LATITUDE`/`FALLBACK_LONGITUDE` in `config.h` if that fails. `weather_task` (in `main.cpp`) re-fetches every 15 minutes (30 s retry on failure) and caches the last successful result via `weather_set_last()`/`weather_get_last()` so `forecast_screen.cpp` can read it without a network round trip.

The sun-path arc on the main screen is a full 24h day/night loop, not just the daylight portion: geometry constants (`SUN_ARC_CX/CY/RADIUS` in `main.cpp`) define a circle whose horizontal diameter is the horizon — the marker travels 180°→0° over the top from sunrise to sunset (day), then continues 0°→-180° under the bottom from sunset back to the next sunrise (night), swapping between the sun and moon icon assets (`weather_icons_data.c`) at each crossing.

### Stock tracking (stocks.cpp, stocks_store.cpp, stocks_screen.cpp)

Same "no API key" philosophy as weather: uses Yahoo Finance's unofficial endpoints (`query1.finance.yahoo.com/v1/finance/search` for symbol/company search, `.../v8/finance/chart/{symbol}` for a single quote) with a browser `User-Agent` header, since Yahoo blocks obviously non-browser requests. `stocks_task` (in `main.cpp`) polls the up-to-3 pinned symbols every 60 s, or immediately after a pin/unpin (`stocks_store_set_changed_cb`), and rebuilds the header's stock card via `update_stock_card()` (clean+rebuild every time). Running this as a second concurrent HTTPS client (alongside `weather_task`) is what surfaced the SPIRAM/mbedtls tuning in `sdkconfig.defaults` — see the comments there for the RAM-contention history.

### Photo slideshow (sd_card.cpp, photo_slideshow_screen.cpp, tools/convert_photos.py)

Tap the time card to open a full-screen slideshow of photos read off the microSD card; tap anywhere to return. No PNG/JPEG decoder is enabled in this firmware, so photos can't just be copied onto the card as-is — `tools/convert_photos.py` (run on a PC, needs `pip install pillow numpy`) crops/scales each photo to exactly 800x480 ("cover" — fills the screen, crops the overflow, no letterboxing) and writes it as a raw LVGL-binary `.bin` file: a 4-byte `lv_img_header_t` bitfield header (`cf=LV_IMG_CF_TRUE_COLOR`, `w=800`, `h=480`) followed by row-major RGB565 pixel data. Copy the script's output `.bin` files into a `/photos` folder on the card.

`sd_card_init()` mounts the card (FATFS over SPI) at `/sdcard`; LVGL's `CONFIG_LV_USE_FS_STDIO` driver (letter `S`, see `sdkconfig.defaults`) maps that mount point onto plain `fopen()`/`fread()`, so `photo_slideshow_screen.cpp` just does `lv_img_set_src(img, "S:/photos/0001.bin")` — LVGL's built-in decoder streams the image row-by-row from the card as it renders, no need to buffer a whole ~750 KB frame in RAM.

**The SD card's SPI pins (`sd_card.cpp`: CS=GPIO10, MOSI=GPIO11, SCLK=GPIO12, MISO=GPIO13) are from community documentation for this board, not empirically verified against this specific unit** — unlike every pin in `lcd.h`, which was confirmed against real hardware. If the card fails to mount, check these against the board's silkscreen first (they're already confirmed to not collide with any RGB/touch pin actually in use).

**Two silent failure modes, both worth knowing before re-debugging this from scratch:**
- ESP-IDF's FATFS defaults to long-filename support *off* (`LFN_NONE`), so an 8.3-compatible name like `0001.bin` comes back from `readdir()` as `0001.BIN` — all-uppercase, short-name style. `photo_slideshow_screen.cpp`'s directory scan is case-insensitive to still find these, but that alone wasn't enough:
- **LVGL's built-in image decoder does its own *case-sensitive* `.bin` extension check** (`lv_img_decoder.c`: `strcmp(lv_fs_get_ext(src), "bin")`), and unlike almost every other failure path in that file, **this one has no `LV_LOG_WARN` call at all** — a `.BIN` file is rejected completely silently, even with `CONFIG_LV_USE_LOG` enabled. This is why the slideshow's first real-hardware test showed a plain black screen with zero error anywhere in the log. Fix: `scan_photos()` lower-cases every filename it stores (FAT's own file lookup is case-insensitive regardless, so this doesn't affect actually finding the file on disk) — don't "fix" this by chasing an LVGL log line that will never appear.

`CONFIG_LV_USE_LOG` (+ `LV_LOG_LEVEL_WARN` + `LV_LOG_PRINTF`) is enabled in `sdkconfig.defaults` specifically because of how long the above took to find with LVGL logging off — it's cheap (silent on the happy path) and worth keeping on for any future work that touches image/file loading.

If a future photo needs a different color/behavior and this binary format needs revisiting, the authoritative reference is `lv_img_header_t` in `managed_components/lvgl__lvgl/src/draw/lv_img_buf.h` and `lv_color16_t` in `.../src/misc/lv_color.h` — both were used to derive the exact byte layout in `convert_photos.py`, and validated once by hand (decoding a converted file's header + sample pixels back in Python and checking against the source image) — don't skip that check when regenerating this format.

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
