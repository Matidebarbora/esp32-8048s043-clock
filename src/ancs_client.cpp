#include "ancs_client.h"

#include <cstdio>
#include <cstring>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_log.h"

#include "notification_store.h"
#include "notification_filter_settings.h"

static const char *TAG = "ancs";
static const char *DEVICE_NAME = "ESP32-Clock";

// ── ANCS UUIDs (Apple Notification Center Service) ──────────────────────────
// Written here in the human-readable byte order from Apple's spec; converted
// to the on-the-wire little-endian esp_bt_uuid_t form by uuid128_from_be().
static const uint8_t ANCS_SVC_UUID_BE[16] = {
    0x79, 0x05, 0xF4, 0x31, 0xB5, 0xCE, 0x4E, 0x99, 0xA4, 0x0F, 0x4B, 0x1E, 0x12, 0x2D, 0x00, 0xD0};
static const uint8_t ANCS_NOTIF_SOURCE_UUID_BE[16] = {
    0x9F, 0xBF, 0x12, 0x0D, 0x63, 0x01, 0x42, 0xD9, 0x8C, 0x58, 0x25, 0xE6, 0x99, 0xA2, 0x1D, 0xBD};
static const uint8_t ANCS_CONTROL_POINT_UUID_BE[16] = {
    0x69, 0xD1, 0xD8, 0xF3, 0x45, 0xE1, 0x49, 0xA8, 0x98, 0x21, 0x9B, 0xBD, 0xFD, 0xAA, 0xD9, 0xD9};
static const uint8_t ANCS_DATA_SOURCE_UUID_BE[16] = {
    0x22, 0xEA, 0xC6, 0xE9, 0x24, 0xD6, 0x4B, 0xB5, 0xBE, 0x44, 0xB3, 0x6A, 0xCE, 0x7C, 0x7B, 0xFB};

// Standard Bluetooth SIG 16-bit UUIDs (GAP service + its Device Name
// characteristic) — used only to read the phone's display name for the
// clock's UI, unrelated to ANCS itself.
#define GAP_SVC_UUID         0x1800
#define GAP_DEVICE_NAME_UUID 0x2A00

static void uuid128_from_be(esp_bt_uuid_t *out, const uint8_t be[16])
{
    out->len = ESP_UUID_LEN_128;
    for (int i = 0; i < 16; i++) out->uuid.uuid128[i] = be[15 - i];
}

static const char *category_name(uint8_t cat)
{
    switch (cat) {
        case 0:  return "Other";
        case 1:  return "IncomingCall";
        case 2:  return "MissedCall";
        case 3:  return "Voicemail";
        case 4:  return "Social";
        case 5:  return "Schedule";
        case 6:  return "Email";
        case 7:  return "News";
        case 8:  return "HealthAndFitness";
        case 9:  return "BusinessAndFinance";
        case 10: return "Location";
        case 11: return "Entertainment";
        default: return "Unknown";
    }
}

// ── Connection / handle state ────────────────────────────────────────────────
// gatts is only used to catch the physical-link CONNECT event (we expose no
// services of our own); the actual ANCS work happens over gattc, attached to
// that same link via esp_ble_gattc_open(..., is_direct=true).
static esp_gatt_if_t s_gatts_if = ESP_GATT_IF_NONE;
static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static uint16_t      s_conn_id  = 0;
static esp_bd_addr_t s_remote_bda;

// ANCS discovery needs both an encrypted/bonded link (a->success from
// AUTH_CMPL) and an attached GATTC connection (ESP_GATTC_OPEN_EVT) — these
// complete asynchronously in no guaranteed order, so track both and start
// once whichever finishes last arrives.
static bool s_bonded      = false;
static bool s_gattc_ready = false;

static uint16_t s_ancs_start_handle    = 0;
static uint16_t s_ancs_end_handle      = 0;
static uint16_t s_notif_source_handle  = 0;
static uint16_t s_notif_source_cccd    = 0;
static uint16_t s_data_source_handle   = 0;
static uint16_t s_data_source_cccd     = 0;
static uint16_t s_control_point_handle = 0;

// GAP service discovery — read once per connection, purely to show the
// phone's name in the clock's header. Independent of the ANCS handles above.
static uint16_t s_gap_start_handle   = 0;
static uint16_t s_gap_end_handle     = 0;
static uint16_t s_device_name_handle = 0;

// True from the physical link coming up (GATTS_CONNECT_EVT) until it drops
// (GATTS_DISCONNECT_EVT) — read by main.cpp's header Bluetooth indicator.
static bool s_connected = false;
static char s_device_name[32] = "iPhone";  // fallback until the GAP read completes (or if it never does)

// Data Source responses can arrive split across several notifications —
// reassembled here before parsing.
#define DATA_SOURCE_BUF_SIZE 512
static uint8_t s_data_buf[DATA_SOURCE_BUF_SIZE];
static size_t  s_data_buf_len = 0;

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = true,
    .min_interval        = 0x20,
    .max_interval        = 0x40,
    .appearance          = 0,
    .manufacturer_len    = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len    = 0,
    .p_service_data      = nullptr,
    .service_uuid_len    = 0,
    .p_service_uuid      = nullptr,
    .flag                = (uint8_t)(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static esp_ble_adv_params_t adv_params = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x40,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ANCS's AppIdentifier attribute is the bundle ID ("net.whatsapp.WhatsApp"),
// not a display name — take the last dot-separated component as a cheap,
// good-enough approximation ("WhatsApp") without an extra GetAppAttributes
// round trip.
static void prettify_app_name(const char *bundle_id, char *out, size_t out_size)
{
    const char *last_dot = strrchr(bundle_id, '.');
    strlcpy(out, last_dot ? last_dot + 1 : bundle_id, out_size);
    if (out[0] == '\0') strlcpy(out, "App", out_size);
}

// ── Data Source parsing ──────────────────────────────────────────────────────
// Response layout: CommandID(1)=0, NotificationUID(4), then repeated
// [AttributeID(1)][Length(2,LE)][Data(Length bytes, UTF-8, not terminated)].
static void try_parse_notification_attributes()
{
    if (s_data_buf_len < 5) return;
    if (s_data_buf[0] != 0) { s_data_buf_len = 0; return; }  // not a GetNotificationAttributes response

    char app_id[64]   = "";
    char title[40]    = "";
    char message[128] = "";

    size_t pos = 5;
    while (pos + 3 <= s_data_buf_len) {
        uint8_t  attr_id  = s_data_buf[pos];
        uint16_t attr_len = (uint16_t)s_data_buf[pos + 1] | ((uint16_t)s_data_buf[pos + 2] << 8);
        size_t   data_at  = pos + 3;
        if (data_at + attr_len > s_data_buf_len) return;  // incomplete — wait for the next fragment

        const char *src = (const char *)&s_data_buf[data_at];
        if (attr_id == 0)      snprintf(app_id, sizeof(app_id), "%.*s", attr_len, src);
        else if (attr_id == 1) snprintf(title, sizeof(title), "%.*s", attr_len, src);
        else if (attr_id == 3) snprintf(message, sizeof(message), "%.*s", attr_len, src);

        pos = data_at + attr_len;
    }

    char app_name[32];
    prettify_app_name(app_id, app_name, sizeof(app_name));
    ESP_LOGI(TAG, "Notification: [%s] %s - %s", app_name, title, message);

    bool filtered = notification_filter_get_hide_whatsapp() && strcmp(app_name, "WhatsApp") == 0;
    if (filtered)
        ESP_LOGI(TAG, "Filtered out (hide_whatsapp)");
    else
        notification_store_add(app_name, title, message);
    s_data_buf_len = 0;
}

static void handle_data_source(const uint8_t *data, uint16_t len)
{
    if (s_data_buf_len + len > DATA_SOURCE_BUF_SIZE) {
        ESP_LOGW(TAG, "Data Source reassembly buffer overflow, dropping");
        s_data_buf_len = 0;
        return;
    }
    memcpy(&s_data_buf[s_data_buf_len], data, len);
    s_data_buf_len += len;
    try_parse_notification_attributes();
}

// ── Notification Source (8-byte summary) + Control Point request ────────────
static void request_notification_attributes(esp_gatt_if_t gattc_if, uint32_t uid)
{
    if (!s_control_point_handle) return;

    uint8_t cmd[1 + 4 + 1 + 3 + 3];
    size_t  i = 0;
    cmd[i++] = 0;  // CommandID: GetNotificationAttributes
    memcpy(&cmd[i], &uid, 4);
    i += 4;
    cmd[i++] = 0;                     // AttributeID: AppIdentifier (no length field)
    cmd[i++] = 1; cmd[i++] = 32; cmd[i++] = 0;   // AttributeID: Title, max length 32 (LE)
    cmd[i++] = 3; cmd[i++] = 100; cmd[i++] = 0;  // AttributeID: Message, max length 100 (LE)

    s_data_buf_len = 0;  // reset reassembly buffer for the incoming response
    esp_ble_gattc_write_char(gattc_if, s_conn_id, s_control_point_handle, i, cmd,
                              ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM);
}

static void handle_notification_source(esp_gatt_if_t gattc_if, const uint8_t *data, uint16_t len)
{
    if (len < 8) return;

    uint8_t  event_id       = data[0];
    uint8_t  event_flags    = data[1];
    uint8_t  category_id    = data[2];
    uint8_t  category_count = data[3];
    uint32_t uid;
    memcpy(&uid, &data[4], 4);

    static const char *event_names[] = {"Added", "Modified", "Removed"};
    const char *event_name = event_id < 3 ? event_names[event_id] : "?";

    ESP_LOGI(TAG, "Notification %s: category=%s (x%d) uid=%u flags=0x%02x",
             event_name, category_name(category_id), category_count, (unsigned)uid, event_flags);

    bool is_silent = event_flags & 0x01;
    if (event_id == 0 /* Added */ && !is_silent)
        request_notification_attributes(gattc_if, uid);
}

// ── ANCS characteristic discovery ────────────────────────────────────────────
static void resolve_one_char(esp_gatt_if_t gattc_if, const uint8_t uuid_be[16],
                              uint16_t *out_handle, uint16_t *out_cccd_handle)
{
    esp_bt_uuid_t uuid;
    uuid128_from_be(&uuid, uuid_be);

    esp_gattc_char_elem_t elem;
    uint16_t count = 1;
    esp_gatt_status_t st = esp_ble_gattc_get_char_by_uuid(
        gattc_if, s_conn_id, s_ancs_start_handle, s_ancs_end_handle, uuid, &elem, &count);
    if (st != ESP_GATT_OK || count == 0) {
        ESP_LOGW(TAG, "ANCS characteristic not found (status=%d)", st);
        return;
    }
    *out_handle = elem.char_handle;

    if (!out_cccd_handle) return;

    esp_bt_uuid_t cccd_uuid;
    cccd_uuid.len = ESP_UUID_LEN_16;
    cccd_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
    esp_gattc_descr_elem_t descr;
    uint16_t dcount = 1;
    st = esp_ble_gattc_get_descr_by_char_handle(gattc_if, s_conn_id, elem.char_handle, cccd_uuid, &descr, &dcount);
    if (st == ESP_GATT_OK && dcount > 0) *out_cccd_handle = descr.handle;
}

static void resolve_ancs_characteristics(esp_gatt_if_t gattc_if)
{
    resolve_one_char(gattc_if, ANCS_NOTIF_SOURCE_UUID_BE, &s_notif_source_handle, &s_notif_source_cccd);
    resolve_one_char(gattc_if, ANCS_DATA_SOURCE_UUID_BE, &s_data_source_handle, &s_data_source_cccd);
    resolve_one_char(gattc_if, ANCS_CONTROL_POINT_UUID_BE, &s_control_point_handle, nullptr);

    ESP_LOGI(TAG, "ANCS characteristics: notif_source=%d data_source=%d control_point=%d",
             s_notif_source_handle, s_data_source_handle, s_control_point_handle);

    if (s_notif_source_handle) esp_ble_gattc_register_for_notify(gattc_if, s_remote_bda, s_notif_source_handle);
    if (s_data_source_handle)  esp_ble_gattc_register_for_notify(gattc_if, s_remote_bda, s_data_source_handle);
}

static void write_cccd_enable(esp_gatt_if_t gattc_if, uint16_t char_handle)
{
    uint16_t cccd_handle = 0;
    if (char_handle == s_notif_source_handle) cccd_handle = s_notif_source_cccd;
    else if (char_handle == s_data_source_handle) cccd_handle = s_data_source_cccd;
    if (!cccd_handle) return;

    uint8_t enable_notify[2] = {0x01, 0x00};
    esp_ble_gattc_write_char_descr(gattc_if, s_conn_id, cccd_handle, sizeof(enable_notify), enable_notify,
                                    ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM);
}

// Reads the phone's GAP Device Name characteristic — purely cosmetic (shown
// in the clock's header), unrelated to ANCS. Not every phone/OS is
// guaranteed to expose this, so s_device_name just keeps its "iPhone"
// fallback if the characteristic isn't found or the read fails.
static void resolve_device_name(esp_gatt_if_t gattc_if)
{
    esp_bt_uuid_t uuid;
    uuid.len = ESP_UUID_LEN_16;
    uuid.uuid.uuid16 = GAP_DEVICE_NAME_UUID;

    esp_gattc_char_elem_t elem;
    uint16_t count = 1;
    esp_gatt_status_t st = esp_ble_gattc_get_char_by_uuid(
        gattc_if, s_conn_id, s_gap_start_handle, s_gap_end_handle, uuid, &elem, &count);
    if (st != ESP_GATT_OK || count == 0) {
        ESP_LOGW(TAG, "Device Name characteristic not found (status=%d)", st);
        return;
    }
    s_device_name_handle = elem.char_handle;
    esp_ble_gattc_read_char(gattc_if, s_conn_id, s_device_name_handle, ESP_GATT_AUTH_REQ_NO_MITM);
}

// Searches every service on the phone (not just ANCS) so the same pass also
// turns up the standard GAP service used to read its display name — see
// ESP_GATTC_SEARCH_RES_EVT, which tells the two apart by UUID.
static void maybe_start_ancs_discovery()
{
    if (!s_bonded || !s_gattc_ready) return;
    ESP_LOGI(TAG, "Discovering services (gattc_if=%d conn_id=%d)", s_gattc_if, s_conn_id);
    esp_err_t search_err = esp_ble_gattc_search_service(s_gattc_if, s_conn_id, nullptr);
    if (search_err != ESP_OK)
        ESP_LOGE(TAG, "search_service call failed: %s", esp_err_to_name(search_err));
}

static void reset_link_state()
{
    s_bonded = s_gattc_ready = false;
    s_ancs_start_handle = s_ancs_end_handle = 0;
    s_notif_source_handle = s_notif_source_cccd = 0;
    s_data_source_handle = s_data_source_cccd = 0;
    s_control_point_handle = 0;
    s_data_buf_len = 0;
    s_gap_start_handle = s_gap_end_handle = 0;
    s_device_name_handle = 0;
    strlcpy(s_device_name, "iPhone", sizeof(s_device_name));
}

// ── GAP: advertising + pairing/bonding ───────────────────────────────────────
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS)
                ESP_LOGI(TAG, "Advertising as \"%s\" — pair from iPhone Settings > Bluetooth", DEVICE_NAME);
            else
                ESP_LOGE(TAG, "Advertising failed to start, status=%d", param->adv_start_cmpl.status);
            break;

        case ESP_GAP_BLE_SEC_REQ_EVT:
            ESP_LOGI(TAG, "Security request from iPhone — accepting");
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;

        case ESP_GAP_BLE_NC_REQ_EVT:
            ESP_LOGI(TAG, "Numeric comparison request — auto-confirming (Just Works)");
            esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT: {
            auto *a = &param->ble_security.auth_cmpl;
            if (!a->success) {
                ESP_LOGW(TAG, "Pairing failed, reason=%d", a->fail_reason);
                break;
            }
            ESP_LOGI(TAG, "Bonded with iPhone (auth_mode=0x%02x)", a->auth_mode);
            s_bonded = true;
            maybe_start_ancs_discovery();
            break;
        }

        default:
            break;
    }
}

// ── GATTS: only used to catch the physical link (conn_id + remote_bda) ──────
// We expose no services of our own — this app exists purely so Bluedroid
// hands us the connection details, which are then used directly on the
// GATTC side (conn_id identifies the physical link, not a per-profile one).
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT:
            s_gatts_if = gatts_if;
            ESP_LOGI(TAG, "GATTS app registered: status=%d gatts_if=%d", param->reg.status, gatts_if);
            break;

        case ESP_GATTS_CONNECT_EVT:
            memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            s_connected = true;
            ESP_LOGI(TAG, "Physical link up — requesting pairing and attaching GATT client");
            // ANCS needs an encrypted, bonded link, but nothing about our own
            // (service-less) GATT server forces the phone to encrypt on its
            // own — request it ourselves so the iPhone shows its "Pair"
            // confirmation regardless of which app initiated the connection.
            esp_ble_set_encryption(s_remote_bda, ESP_BLE_SEC_ENCRYPT_NO_MITM);
            // is_direct=true: the link already physically exists (the phone
            // just connected to us), and this reuses it rather than starting
            // a new connection attempt — is_direct=false ("background/auto
            // connect") is a different mechanism and isn't usable here (it
            // errors immediately with "Unsupported transport").
            esp_ble_gattc_open(s_gattc_if, s_remote_bda, param->connect.ble_addr_type, true);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGW(TAG, "iPhone disconnected — resuming advertising");
            s_connected = false;
            reset_link_state();
            esp_ble_gap_start_advertising(&adv_params);
            break;

        default:
            break;
    }
}

// ── GATTC: ANCS service discovery + notification handling ───────────────────
static void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTC_REG_EVT:
            s_gattc_if = gattc_if;
            ESP_LOGI(TAG, "GATTC app registered: status=%d gattc_if=%d", param->reg.status, gattc_if);
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "GATTC open failed, status=%d", param->open.status);
                break;
            }
            s_conn_id     = param->open.conn_id;
            s_gattc_ready = true;
            ESP_LOGI(TAG, "GATT client attached (conn_id=%d, mtu=%d)", s_conn_id, param->open.mtu);
            maybe_start_ancs_discovery();
            break;

        case ESP_GATTC_SEARCH_RES_EVT: {
            // Unfiltered search now (see maybe_start_ancs_discovery) — every
            // service on the phone shows up here, so tell ANCS and GAP apart
            // by comparing the actual UUID, not just its length.
            auto &uuid = param->search_res.srvc_id.uuid;
            ESP_LOGI(TAG, "Search result: uuid_len=%d handles %d-%d",
                     uuid.len, param->search_res.start_handle, param->search_res.end_handle);

            esp_bt_uuid_t ancs_uuid;
            uuid128_from_be(&ancs_uuid, ANCS_SVC_UUID_BE);
            if (uuid.len == ESP_UUID_LEN_128 && memcmp(uuid.uuid.uuid128, ancs_uuid.uuid.uuid128, 16) == 0) {
                s_ancs_start_handle = param->search_res.start_handle;
                s_ancs_end_handle   = param->search_res.end_handle;
                ESP_LOGI(TAG, "Found ANCS service, handles %d-%d", s_ancs_start_handle, s_ancs_end_handle);
            } else if (uuid.len == ESP_UUID_LEN_16 && uuid.uuid.uuid16 == GAP_SVC_UUID) {
                s_gap_start_handle = param->search_res.start_handle;
                s_gap_end_handle   = param->search_res.end_handle;
                ESP_LOGI(TAG, "Found GAP service, handles %d-%d", s_gap_start_handle, s_gap_end_handle);
            }
            break;
        }

        case ESP_GATTC_SEARCH_CMPL_EVT:
            ESP_LOGI(TAG, "Service search complete: status=%d", param->search_cmpl.status);
            if (s_ancs_start_handle == 0)
                ESP_LOGW(TAG, "ANCS service not found on this connection");
            else
                resolve_ancs_characteristics(gattc_if);

            if (s_gap_start_handle == 0)
                ESP_LOGW(TAG, "GAP service not found — keeping the \"iPhone\" fallback name");
            else
                resolve_device_name(gattc_if);
            break;

        case ESP_GATTC_READ_CHAR_EVT:
            if (param->read.handle == s_device_name_handle && param->read.status == ESP_GATT_OK) {
                size_t len = param->read.value_len < sizeof(s_device_name) - 1
                                 ? param->read.value_len : sizeof(s_device_name) - 1;
                memcpy(s_device_name, param->read.value, len);
                s_device_name[len] = '\0';
                ESP_LOGI(TAG, "Device name: %s", s_device_name);
            }
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            if (param->reg_for_notify.status != ESP_GATT_OK)
                ESP_LOGE(TAG, "register_for_notify failed for handle %d", param->reg_for_notify.handle);
            else
                write_cccd_enable(gattc_if, param->reg_for_notify.handle);
            break;

        case ESP_GATTC_WRITE_DESCR_EVT:
            if (param->write.status != ESP_GATT_OK)
                ESP_LOGE(TAG, "CCCD write failed for handle %d, status=%d", param->write.handle, param->write.status);
            else
                ESP_LOGI(TAG, "Notifications enabled for handle %d", param->write.handle);
            break;

        case ESP_GATTC_WRITE_CHAR_EVT:
            if (param->write.status != ESP_GATT_OK)
                ESP_LOGE(TAG, "Control Point write failed, status=%d", param->write.status);
            break;

        case ESP_GATTC_NOTIFY_EVT:
            if (param->notify.handle == s_notif_source_handle)
                handle_notification_source(gattc_if, param->notify.value, param->notify.value_len);
            else if (param->notify.handle == s_data_source_handle)
                handle_data_source(param->notify.value, param->notify.value_len);
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            reset_link_state();
            break;

        default:
            break;
    }
}

void ancs_client_init(void)
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err;

    err = esp_bt_controller_init(&bt_cfg);
    if (err) { ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(err)); return; }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err) { ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(err)); return; }

    err = esp_bluedroid_init();
    if (err) { ESP_LOGE(TAG, "bluedroid init failed: %s", esp_err_to_name(err)); return; }

    err = esp_bluedroid_enable();
    if (err) { ESP_LOGE(TAG, "bluedroid enable failed: %s", esp_err_to_name(err)); return; }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gattc_register_callback(gattc_event_handler);
    esp_ble_gatts_app_register(0);
    esp_ble_gattc_app_register(0);

    // Just Works pairing + bonding: no PIN/passkey UI needed, iPhone just
    // shows a "Pair" confirmation. Bonding is required for ANCS access.
    esp_ble_io_cap_t   iocap    = ESP_IO_CAP_NONE;
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

    esp_ble_gap_set_device_name(DEVICE_NAME);
    esp_ble_gap_config_adv_data(&adv_data);

    ESP_LOGI(TAG, "BLE stack up — advertising as \"%s\", waiting for iPhone to pair", DEVICE_NAME);
}

bool ancs_client_is_connected(void)
{
    return s_connected;
}

const char *ancs_client_get_device_name(void)
{
    return s_device_name;
}
