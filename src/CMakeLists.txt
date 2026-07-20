idf_component_register(
    SRCS "main.cpp" "lcd.cpp" "wifi_time.cpp" "wifi_settings.cpp" "wifi_store.cpp" "app_settings.cpp" "settings_screen.cpp" "season_time_screen.cpp" "weather.cpp" "forecast_screen.cpp" "weather_icon.cpp" "weather_icons_data.c" "geolocation.cpp" "time_digits_data.c" "bulb_icon_data.c" "dimmer_settings.cpp" "dimmer_screen.cpp" "background_settings.cpp" "background_screen.cpp" "stocks.cpp" "stocks_store.cpp" "stocks_screen.cpp" "sd_card.cpp" "photo_slideshow_screen.cpp"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_lcd driver esp_wifi esp_netif esp_event nvs_flash esp_http_client mbedtls json fatfs sdmmc esp_driver_sdspi esp_driver_spi
)
