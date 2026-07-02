#pragma once
#include "wifi_time.h"

// ── WiFi networks tried in order until one connects ───────────────────────────
static const wifi_network_t WIFI_NETWORKS[] = {
    { "YourSSID",    "YourPassword"  },
    // { "BackupSSID", "BackupPassword" },
};

// ── NTP server ────────────────────────────────────────────────────────────────
#define NTP_SERVER "pool.ntp.org"

// Time zone is no longer a fixed value here — it's chosen at runtime via the
// Settings screen (Winter/Summer time) and persisted across reboots.
// See app_settings.h.

// ── Fallback location for the weather card (Open-Meteo) ────────────────────────
// Location is normally auto-detected via IP geolocation (see geolocation.h).
// These are only used if that lookup fails. Defaults to Buenos Aires.
#define FALLBACK_LATITUDE  (-34.6037f)
#define FALLBACK_LONGITUDE (-58.3816f)
