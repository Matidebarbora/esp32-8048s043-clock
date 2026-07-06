#pragma once
#include "esp_err.h"
#include <stdbool.h>

#define SD_MOUNT_POINT "/sdcard"
#define SD_PHOTOS_DIR  SD_MOUNT_POINT "/photos"

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the microSD card (FATFS over SPI) at SD_MOUNT_POINT. Safe to call
// once at startup — if no card is present (or the mount otherwise fails),
// logs a warning and returns an error instead of blocking boot;
// photo_slideshow_screen.cpp just reports "no photos" in that case.
esp_err_t sd_card_init(void);

// True once sd_card_init() has mounted the card successfully.
bool sd_card_is_mounted(void);

#ifdef __cplusplus
}
#endif
