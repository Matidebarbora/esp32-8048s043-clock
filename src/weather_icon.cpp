#include "weather_icon.h"
#include "weather_icons_data.h"

// Icons are Twemoji SVGs (CC-BY 4.0, https://github.com/twitter/twemoji)
// rendered offline at the exact target resolutions and converted to LVGL's
// TRUE_COLOR_ALPHA format — see weather_icons_data.c.
enum weather_icon_kind_t { WI_SUN, WI_PARTLY_CLOUDY, WI_CLOUD, WI_FOG, WI_RAIN, WI_SNOW, WI_STORM };

// Groups WMO codes (see weather_code_description() in weather.cpp) into the
// icon set we have images for.
static weather_icon_kind_t classify(int code)
{
    switch (code) {
        case 0: case 1:             return WI_SUN;
        case 2:                     return WI_PARTLY_CLOUDY;
        case 3:                     return WI_CLOUD;
        case 45: case 48:           return WI_FOG;
        case 71: case 73: case 75:
        case 77: case 85: case 86:  return WI_SNOW;
        case 95: case 96: case 99:  return WI_STORM;
        default:                    return WI_RAIN;  // drizzle/rain/showers
    }
}

static const lv_img_dsc_t *pick(weather_icon_kind_t kind, lv_coord_t size, bool is_night)
{
    bool small = size <= 32;
    switch (kind) {
        case WI_SUN:
            if (is_night) return small ? &img_weather_moon_32 : &img_weather_moon_100;
            return small ? &img_weather_sun_32 : &img_weather_sun_100;
        case WI_PARTLY_CLOUDY: return small ? &img_weather_partly_cloudy_32 : &img_weather_partly_cloudy_100;
        case WI_CLOUD:         return small ? &img_weather_cloud_32         : &img_weather_cloud_100;
        case WI_FOG:           return small ? &img_weather_fog_32           : &img_weather_fog_100;
        case WI_RAIN:          return small ? &img_weather_rain_32          : &img_weather_rain_100;
        case WI_SNOW:          return small ? &img_weather_snow_32          : &img_weather_snow_100;
        case WI_STORM:         return small ? &img_weather_storm_32         : &img_weather_storm_100;
        default:               return &img_weather_cloud_100;
    }
}

// ── Lightweight animation, main (100px) icon only ───────────────────────────
// Forecast-row/hourly-strip icons (32px, see the `size > 32` gate below)
// stay static, both to keep those dense lists calm and to limit how much
// extra periodic redraw this adds. Sun rotates continuously; everything
// else (moon, clouds, rain, snow, fog, storm) gently bobs up and down.
//
// A 16-frame pre-baked sprite-cycle version of the sun rotation was tried
// and rejected in favor of this one: with only 16 frames, matching this
// version's slow ~36s/turn would look like visible discrete jumps rather
// than a spin — sprites needed a much faster ~3.2s/turn (16 frames * 200ms)
// to look smooth, which read as too fast. More frames would fix that at a
// flash-size cost (30 KB per 100x100 frame) but weren't worth it for how
// this one already looks — don't re-attempt the sprite approach for pure
// rotation without a reason to revisit that tradeoff.
//
// Runs on a slow (200ms, ~5 steps/sec) lv_timer rather than LVGL's
// smooth-by-default lv_anim (which would tick every 20ms task-handler
// cycle, ~50/sec): full_refresh=1 means *any* invalidation forces a
// complete 800x480 SW re-render (see CLAUDE.md's RGB panel timing notes on
// pixel-clock margin under heavy redraw), so this deliberately trades away
// silky-smooth motion for a redraw cadence closer to the clock's own
// already-proven-stable 1/sec tick.
struct IconAnimState {
    lv_obj_t   *img;
    lv_timer_t *timer;
    bool        rotate;    // true: spin (sun only); false: bob
    int16_t     angle;     // 0.1-degree units, per lv_img_set_angle()
    lv_coord_t  bob_y;
    int8_t      bob_dir;
};

#define ICON_ANIM_PERIOD_MS 200

static void icon_anim_timer_cb(lv_timer_t *timer)
{
    auto *st = (IconAnimState *)timer->user_data;
    if (st->rotate) {
        st->angle = (st->angle + 20) % 3600;  // 2°/tick -> full turn in ~36s
        lv_img_set_angle(st->img, st->angle);
    } else {
        st->bob_y += st->bob_dir;
        if (st->bob_y >= 4 || st->bob_y <= -4) st->bob_dir = (int8_t)-st->bob_dir;
        lv_obj_set_style_translate_y(st->img, st->bob_y, 0);  // visual-only offset, doesn't disturb the parent's flex layout
    }
}

// The icon is rebuilt from scratch whenever the weather code or day/night
// state changes (see update_sun_arc() in main.cpp: lv_obj_clean() + a fresh
// weather_icon_create() call) — free the timer and its state right along
// with the image object instead of leaking one every time that happens.
static void icon_anim_cleanup_cb(lv_event_t *e)
{
    auto *st = (IconAnimState *)lv_event_get_user_data(e);
    lv_timer_del(st->timer);
    delete st;
}

static void attach_animation(lv_obj_t *img, weather_icon_kind_t kind, bool is_night)
{
    auto *st    = new IconAnimState{};
    st->img     = img;
    st->rotate  = (kind == WI_SUN && !is_night);
    st->bob_dir = 1;
    st->timer   = lv_timer_create(icon_anim_timer_cb, ICON_ANIM_PERIOD_MS, st);
    lv_obj_add_event_cb(img, icon_anim_cleanup_cb, LV_EVENT_DELETE, st);
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, int weather_code, lv_coord_t size, bool is_night)
{
    lv_obj_t *img = lv_img_create(parent);
    weather_icon_kind_t kind = classify(weather_code);
    lv_img_set_src(img, pick(kind, size, is_night));

    if (size > 32) attach_animation(img, kind, is_night);

    return img;
}
