#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts the BLE stack, advertises as a pairable accessory, and once bonded
// with the iPhone, connects to Apple's ANCS (Notification Center Service) as
// a GATT client — receiving live notification metadata (app, title, message,
// category) for calls, texts, mail, and any other app's notifications.
//
// Must be called after nvs_flash_init() — Bluedroid needs NVS up front to
// persist bonding data, otherwise the iPhone has to be re-paired on every
// reboot.
//
// Pairing: on the iPhone, go to Settings > Bluetooth and tap the device name
// (see DEVICE_NAME in ancs_client.cpp) to connect — this triggers a "Pair"
// confirmation on the phone (no PIN, Just Works pairing). Everything else
// happens automatically afterwards. Logs via ESP_LOGI, tag "ancs".
void ancs_client_init(void);

#ifdef __cplusplus
}
#endif
