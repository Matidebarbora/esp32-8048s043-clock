#pragma once
#include <stdbool.h>

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

// True once the phone's physical BLE link is up (from ESP_GATTS_CONNECT_EVT
// until ESP_GATTS_DISCONNECT_EVT) — doesn't require ANCS discovery to have
// finished. Safe to call from any task; backed by a plain flag written from
// the Bluedroid callback thread, same informal thread-safety as the rest of
// this codebase's simple status flags (e.g. sd_card_is_mounted()).
bool ancs_client_is_connected(void);

// The connected phone's name, read from its GAP service (Device Name
// characteristic, UUID 0x2A00) once available. Returns a fallback ("iPhone")
// before that read completes or if the phone doesn't expose it.
const char *ancs_client_get_device_name(void);

#ifdef __cplusplus
}
#endif
