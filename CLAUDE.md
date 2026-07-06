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
| `notification_filter_settings.cpp` / `notification_filter_screen.cpp` | NVS-backed per-app notification toggles (currently just "hide WhatsApp") + its Settings sub-screen (one switch row per app) — same NVS-settings/UI-screen split as `app_settings`/`season_time_screen` and `dimmer_settings`/`dimmer_screen` |
| `dimmer_settings.cpp` / `dimmer_screen.cpp` | NVS-backed backlight dimmer (OFF/ON/AUTO with scheduled start/end times) + its settings sub-screen |
| `weather.cpp` / `weather_icon.cpp` / `weather_icons_data.c` | Open-Meteo fetch/parse + cache, WMO-code-to-icon classification, Twemoji-derived icon bitmaps (sun/moon/cloud/rain/snow/storm, 100px + 32px) |
| `geolocation.cpp` | One-time IP-based geolocation (ip-api.com) for the weather card's coordinates |
| `forecast_screen.cpp` | 5-day forecast screen (tap the weather card) |
| `time_digits_data.c` / `bulb_icon_data.c` / `app_icons_data.c` | Generated image assets — big clock digits, brightness bulb icon, WhatsApp/Gmail notification icons (see "Custom fonts" below for the generation pipeline pattern; icons follow the same offline Python approach) |
| `ancs_client.cpp` | BLE stack init + pairing/bonding with the iPhone + ANCS (Apple Notification Center Service) GATT client — see "Bluetooth / ANCS notifications" below |
| `notification_store.cpp` | Thread-safe in-memory notification history (ANCS callback thread writes, LVGL thread reads), avatar color/initials helpers |
| `notification_icon.cpp` | Builds a notification avatar: real brand icon (WhatsApp/Gmail) or colored-circle-with-initials fallback |
| `notification_screen.cpp` | Full notification history screen (tap the notification card), "remove all" |
| `es_fonts.h` / `lv_font_montserrat_{16,20}_es.c` | Spanish-accented-character font variants — see "Custom fonts" below |

## Architecture

### Threading model

`LCDInit()` spawns an LVGL task pinned to **core 1** that calls `lv_tick_inc(20)` then `lv_task_handler()` every 20 ms.  `app_main` runs on **core 0**.  Any LVGL call from `app_main` must be wrapped with `lvgl_acquire()` / `lvgl_release()`, which take/give a mutex (`xGuiSemaphore`) checked by task-handle identity so the LVGL task can call LVGL without deadlock. The same rule applies to every other task that touches the UI: `weather_task`, `wifi_reconnect_task`, `dimmer_task`, and the ANCS BLE callback (via `notification_store`'s changed-callback, see below).

### Startup sequence

`app_main` runs strictly sequentially:
1. `nvs_flash_init()` (with erase-and-retry on version mismatch) — must run before `ancs_client_init()`, since Bluedroid needs NVS up front to persist BLE bonding data (otherwise the iPhone has to be re-paired every reboot). Safe to call again later — `wifi_store`/`app_settings`/`dimmer_settings` each call it too; it's idempotent.
2. `LCDInit()` — RGB panel + GT911 touch (direct I²C) + LVGL init + LVGL task; backlight on
3. `ancs_client_init()` — BLE advertising + ANCS client starts in the background (pairing happens whenever the phone connects, independent of the rest of boot)
4. `build_ui()` + `clock_tick_cb` (1 s) + `wifi_status_tick_cb` (2 s) timers
5. `app_settings_init()` — loads DST mode, sets `TZ` (must happen before any time read)
6. `wifi_store_init()` + `build_connect_candidates()`, `wifi_time_init()` (creates the connect mutex — must happen before any task that might call `wifi_connect_any()`/`wifi_connect_one()` is spawned)
7. `dimmer_settings_init()` + change-callback, `notification_store` change-callback
8. Spawn `weather_task`, `wifi_reconnect_task`, `dimmer_task`
9. `wifi_connect_any()` (10 s budget) → on success, `time_sync()` (SNTP). On WiFi/NTP failure, time and date are simply left blank — no manual entry fallback; `clock_tick_cb()` no-ops while the system clock is unset (`tm_year <= 70`)
10. `vTaskSuspend(NULL)` — everything from here on is timer/task driven, `app_main` is no longer needed

### LVGL configuration

LVGL is configured via Kconfig (`sdkconfig.defaults`) — `CONFIG_LV_CONF_SKIP=y` means there is **no** `lv_conf.h`. To enable additional fonts or widgets, add the corresponding `CONFIG_LV_*` line and delete `.pio/build` (see the sdkconfig-regeneration gotcha below — this applies to *any* new Kconfig option, not just fonts).

`lv_tick_inc(20)` **must** be called every LVGL task iteration — without it, LVGL timers and the indev poll never fire (display freezes, touch never reported).

Fonts currently enabled: Montserrat 14, 16, 20, 24, 28, 32, 48 — plus two custom Spanish-accented variants, see "Custom fonts" below.

**Editing `sdkconfig.defaults` alone is not enough.** The persisted `sdkconfig.esp32s3-clock` file does **not** auto-regenerate new options from `sdkconfig.defaults` once it already exists — it only fills in options that weren't set before. After adding/changing anything in `sdkconfig.defaults`, delete **both** `.pio/build` **and** `sdkconfig.esp32s3-clock`, then rebuild. This has bitten every Kconfig change in this project (fonts, then Bluetooth, then the SPIRAM/mbedtls memory options) — check for it first whenever a new `CONFIG_*` setting doesn't seem to take effect.

### Dependencies

| Component | Version | Purpose |
|---|---|---|
| `lvgl/lvgl` | 8.3.11 | UI framework |
| `bt` (ESP-IDF built-in, Bluedroid host) | — | BLE advertising, pairing/bonding, GATT client — see "Bluetooth / ANCS notifications" |

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

### Bluetooth / ANCS notifications (ancs_client.cpp)

The board advertises as a connectable BLE peripheral ("ESP32-Clock"). The iPhone connects to it (via Settings > Bluetooth, or any BLE central app — **not** discoverable through Settings' "Other Devices" list, that only surfaces a narrow set of recognized accessory categories, so use a scanner app like nRF Connect/LightBlue to confirm advertising works). Once bonded, `ancs_client.cpp` acts as a GATT **client** against Apple's ANCS service (UUID `7905F431-...`) to receive live notification metadata (app, title, message, category) — see the ANCS spec for the wire format (8-byte Notification Source summary + Control Point `GetNotificationAttributes` request + Data Source response, which can arrive split across several BLE notifications and needs reassembly).

Hard-won lessons, in the order they'd bite again:

- **Pairing must be requested by the accessory, not just accepted.** The ESP32 only had a handler for *incoming* security requests; nothing ever asked the iPhone to pair, so no prompt ever appeared even when a central connected. Fix: call `esp_ble_set_encryption(remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM)` right after the physical link forms (`ESP_GATTS_CONNECT_EVT`), which is what triggers the native "Bluetooth Pairing Request" popup on the phone regardless of which app initiated the connection.
- **`esp_ble_gattc_open(..., is_direct=false)` does not mean "attach to the existing link."** It means "background auto-connect" (a whitelist-based reconnect mechanism) — a different feature entirely, and calling it that way fails immediately with "Unsupported transport." To act as a GATT client over a link the *phone* initiated (we're the peripheral), register a **GATTS** app purely to catch `ESP_GATTS_CONNECT_EVT` (no services need to be exposed) — its `conn_id` identifies the physical link and is directly usable in **GATTC** calls (`esp_ble_gattc_search_service` etc.) without ever calling `esp_ble_gattc_open()`. (An `esp_ble_gattc_open(..., is_direct=true)` also works — it just reuses the existing ACL link rather than erroring — but the GATTS-catch approach needs no extra attach step at all.)
- **Only start GATT work after `ESP_GAP_BLE_AUTH_CMPL_EVT` succeeds**, not right after the physical connection — ANCS requires an encrypted, bonded link, and service discovery silently produces nothing (no error, no results, just no further events) if attempted too early.
- **This target defaults to the BLE 5.0 extended-advertising API**, which is mutually exclusive with the legacy 4.2 `esp_ble_gap_start_advertising()`/`start_scanning()` calls this code uses — see `CONFIG_BT_BLE_50_FEATURES_SUPPORTED=n` / `CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y` in `sdkconfig.defaults`.
- **BLE + WiFi + TLS together starve internal RAM** enough to fail `mbedtls_ssl_setup` mid-handshake once the BT stack is up (this project has 8 MB of PSRAM but mbedtls' default buffers and much of the BT stack default to internal SRAM). Fixed via `CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y` + `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` + a smaller `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` — see `sdkconfig.defaults`. That fix was only tested with one concurrent HTTPS client (`weather_task`); adding a second one (`stocks_task`/`stocks_screen.cpp`, hitting Yahoo Finance) reintroduced the exact same failure (`Dynamic Impl: alloc(...) failed` / `mbedtls_ssl_handshake returned -0x7F00`) even though 8 MB of PSRAM sat idle — mbedtls's dynamic SSL buffers are a few KB each, under `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`'s default 16 KB threshold, so ESP-IDF's `malloc()` forced them into internal RAM no matter how much PSRAM was free. Fixed by lowering `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to `1024` so mid-size allocations like these can spill into PSRAM instead of fighting Wi-Fi/BT for the same scarce internal SRAM. If a third concurrent network client gets added later and this resurfaces, suspect this threshold (or genuine internal-RAM exhaustion from a growing BT/Wi-Fi footprint) before re-tuning the mbedtls buffer sizes again.
  With that fixed, a *different* failure showed up against Yahoo Finance specifically (`mbedtls_ssl_handshake returned -0x7100` / `MBEDTLS_ERR_SSL_BAD_INPUT_DATA`, not an alloc failure) — `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` was sized for Open-Meteo's handshake messages, and Yahoo's CDN-fronted TLS termination sends a bigger certificate chain that didn't fit. Bumped back up to `8192` (still dynamically freed after use, and — being over the 1024-byte threshold above — still served from PSRAM, not internal RAM). If a future HTTPS endpoint fails the same way, suspect this content-length ceiling before assuming it's another RAM-starvation issue.
- **iOS never exposes the phone's own battery level (or anything else outside ANCS/AMS) to a non-MFi accessory over BLE.** Don't go looking for a battery-service workaround; there isn't one without MFi hardware.

Per-app filtering (`notification_filter_settings.cpp`/`.h`) is checked in `ancs_client.cpp` right where a notification's app name is known, before it reaches `notification_store` — filtered notifications never enter the store, so they don't show on the card, the history screen, or (if you add push-style alerts later) trigger anything else. Currently only WhatsApp has a toggle; add more `get/set` pairs plus a `strcmp` there and a row in `notification_filter_screen.cpp` to extend it.

### Weather (weather.cpp, geolocation.cpp)

Open-Meteo (`api.open-meteo.com`) needs no API key but also has no built-in IP geolocation — coordinates come from a one-time `ip-api.com` lookup at boot (`geolocation.cpp`), falling back to `FALLBACK_LATITUDE`/`FALLBACK_LONGITUDE` in `config.h` if that fails. `weather_task` (in `main.cpp`) re-fetches every 15 minutes (30 s retry on failure) and caches the last successful result via `weather_set_last()`/`weather_get_last()` so `forecast_screen.cpp` can read it without a network round trip.

The sun-path arc on the main screen is a full 24h day/night loop, not just the daylight portion: geometry constants (`SUN_ARC_CX/CY/RADIUS` in `main.cpp`) define a circle whose horizontal diameter is the horizon — the marker travels 180°→0° over the top from sunrise to sunset (day), then continues 0°→-180° under the bottom from sunset back to the next sunrise (night), swapping between the sun and moon icon assets (`weather_icons_data.c`) at each crossing.

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

### Custom fonts (es_fonts.h / lv_font_montserrat_{16,20}_es.c)

The stock `lv_font_montserrat_XX` fonts built into LVGL only cover ASCII (0x20-0x7E) plus a handful of icon glyphs (see the `Opts:` comment at the top of `managed_components/lvgl__lvgl/src/font/lv_font_montserrat_16.c` for the exact `lv_font_conv` invocation) — no accented Latin characters at all, so Spanish notification text silently dropped `á é í ó ú ü ñ ¿ ¡` etc. There's no Kconfig option that fixes this — the prebuilt `.c` files are static binaries baked at LVGL-release time.

Fix: two custom fonts (`lv_font_montserrat_16_es`, `lv_font_montserrat_20_es`) covering ASCII + `¡¿ÁÉÍÑÓÚÜáéíñóúü`, generated without Node.js/`lv_font_conv` (not available in this environment) via a Python pipeline instead:

1. `pip install freetype-py fonttools`
2. Download the Montserrat **variable** font (Google Fonts moved off static per-weight files): `ofl/montserrat/Montserrat[wght].ttf` from `github.com/google/fonts`
3. Instantiate the Medium (500) weight lv_font_conv actually uses: `python -m fontTools.varLib.instancer Montserrat[wght].ttf wght=500 -o Montserrat-Medium.ttf`
4. Render each glyph with FreeType: `FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL | FT_LOAD_NO_HINTING` (hinting snaps metrics to whole pixels — `lv_font_conv` doesn't hint, so this is required to match). Trim rows where every pixel is below a small threshold (~32/255) from the top/bottom of the raw bitmap — FreeType's own AA leaves a near-invisible fringe row `lv_font_conv` doesn't.
5. Pack glyphs into `lv_font_fmt_txt_dsc_t` / `lv_font_t` structs by hand (`LV_FONT_FMT_TXT_PLAIN`, 4bpp, no kerning — `kern_dsc = NULL`) — the struct layouts are in `lv_font_fmt_txt.h`. `line_height`/`base_line` are global per-size constants, not derived from glyphs — copy them straight from the matching stock font file rather than recomputing.
6. **Validate before trusting it**: parse the known-good `glyph_dsc[]` values out of the stock `lv_font_montserrat_16.c` and compare against what the pipeline computes for the same ASCII glyphs. This caught the hinting and AA-fringe issues above — don't skip this step when regenerating.

To add more characters later (another language, more punctuation), extend the accented-character list in the generator script and rerun — the pipeline is not currently checked into the repo (built in a scratch dir), so it needs to be reconstructed from this recipe.
