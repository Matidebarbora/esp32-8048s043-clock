#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// NVS-backed toggles for which notifications ancs_client.cpp stores/shows.
// Currently just WhatsApp; add more get/set pairs here as more filters are
// requested (see notification_filter_screen.cpp for the matching UI row).
void notification_filter_settings_init(void);

bool notification_filter_get_hide_whatsapp(void);
void notification_filter_set_hide_whatsapp(bool hide);

#ifdef __cplusplus
}
#endif
