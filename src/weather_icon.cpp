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

static const lv_img_dsc_t *pick(weather_icon_kind_t kind, lv_coord_t size)
{
    bool small = size <= 32;
    switch (kind) {
        case WI_SUN:           return small ? &img_weather_sun_32           : &img_weather_sun_100;
        case WI_PARTLY_CLOUDY: return small ? &img_weather_partly_cloudy_32 : &img_weather_partly_cloudy_100;
        case WI_CLOUD:         return small ? &img_weather_cloud_32         : &img_weather_cloud_100;
        case WI_FOG:           return small ? &img_weather_fog_32           : &img_weather_fog_100;
        case WI_RAIN:          return small ? &img_weather_rain_32          : &img_weather_rain_100;
        case WI_SNOW:          return small ? &img_weather_snow_32          : &img_weather_snow_100;
        case WI_STORM:         return small ? &img_weather_storm_32         : &img_weather_storm_100;
        default:               return &img_weather_cloud_100;
    }
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, int weather_code, lv_coord_t size)
{
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, pick(classify(weather_code), size));
    return img;
}
