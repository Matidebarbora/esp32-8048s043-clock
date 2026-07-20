#pragma once
#include <stdint.h>
#include <stdbool.h>

// Each option is either a solid color (has_gradient=false, r/g/b only) or a
// vertical gradient from (r,g,b) at the top to (r2,g2,b2) at the bottom.
// Index 0 is always the original solid black — the clock's default before
// this setting existed, and must stay available per product requirement.
// Every option deliberately stays in roughly the same dark, low-luminance
// range as that original (8,8,10): the cards (bg (26,26,30), border
// (46,46,52)) and white text were designed against it, and a much brighter
// background would wreck their contrast.
typedef struct {
    const char *name;
    uint8_t     r, g, b;
    bool        has_gradient;
    uint8_t     r2, g2, b2;
} background_option_t;

extern const background_option_t BACKGROUND_OPTIONS[];
extern const int                 BACKGROUND_OPTION_COUNT;

#ifdef __cplusplus
extern "C" {
#endif

// Initializes NVS (safe even if already initialized elsewhere) and loads the
// persisted background choice. Call once at startup, before the clock
// screen is built — unlike most other _settings modules, this one affects
// the UI's initial construction, not just a later-applied policy.
void background_settings_init(void);

// Index into BACKGROUND_OPTIONS. Always in range — clamps to 0 (black) if
// NVS ever holds something stale (e.g. after BACKGROUND_OPTION_COUNT shrinks).
int background_settings_get_index(void);

// Persists immediately and notifies the registered change callback (if any).
void background_settings_set_index(int index);

// Called after the index changes, so the clock screen can re-apply the
// background immediately. Only one callback is supported (main.cpp).
typedef void (*background_settings_changed_cb_t)(void);
void background_settings_set_changed_cb(background_settings_changed_cb_t cb);

#ifdef __cplusplus
}
#endif
