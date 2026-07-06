#include "sd_card.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

static const char *TAG = "sd_card";

// GPIO assignments for this board's microSD slot (SPI mode), per community
// documentation for the ESP32-8048S043 (not yet verified against this
// specific unit the way lcd.h's RGB/touch pins were empirically confirmed —
// if the card fails to mount, double-check these against the board's
// silkscreen first). Free on this board: no conflict with lcd.h's RGB
// (GPIO 1,3-9,14-16,21,39-48) or touch I2C (GPIO 18-20,38) pins.
#define SD_PIN_CS   GPIO_NUM_10
#define SD_PIN_MOSI GPIO_NUM_11
#define SD_PIN_SCLK GPIO_NUM_12
#define SD_PIN_MISO GPIO_NUM_13

static bool s_mounted = false;

esp_err_t sd_card_init(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = SD_PIN_MOSI;
    bus_cfg.miso_io_num     = SD_PIN_MISO;
    bus_cfg.sclk_io_num     = SD_PIN_SCLK;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    // Sized down from the usual 4000-byte example value: this determines
    // the SPI bus's internal DMA bounce buffer, which — unlike mbedtls's
    // buffers (see the "BLE + WiFi + TLS" note below) — must come from
    // internal RAM; it can't be redirected to PSRAM. With weather_task/
    // stocks_task's HTTPS handshakes also competing for that same scarce
    // internal RAM, a photo read landing at the wrong moment failed with
    // "sdmmc_read_sectors: not enough mem, err=0x101". A smaller bounce
    // buffer is far more likely to still fit even when internal RAM is
    // tight — worth revisiting (or tackling the contention more directly,
    // e.g. serializing SD reads against HTTPS requests) if this recurs.
    bus_cfg.max_transfer_sz = 1024;

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = SD_PIN_CS;
    slot_cfg.host_id = (spi_host_device_t)host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files              = 4;
    mount_cfg.allocation_unit_size   = 16 * 1024;

    sdmmc_card_t *card = nullptr;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No SD card mounted (%s) — photo slideshow will report no photos",
                  esp_err_to_name(ret));
        spi_bus_free((spi_host_device_t)host.slot);
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}
