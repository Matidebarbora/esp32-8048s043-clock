#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spanish-extended Montserrat Medium, sizes 16 and 20 — the stock
// lv_font_montserrat_XX built into LVGL (see managed_components/lvgl__lvgl/
// src/font/) only covers ASCII (0x20-0x7E); notification text from the
// phone regularly contains Spanish accented characters, which silently
// failed to render with those fonts. These add: ¡ ¿ Á É Í Ñ Ó Ú Ü á é í ñ ó
// ú ü, generated offline from the same Montserrat-Medium.ttf lv_font_conv
// would use (see CLAUDE.md for the regeneration recipe) — everything else
// (metrics, bpp, glyph format) matches the stock fonts, just without the
// LV_SYMBOL_* icon glyphs, which these text-only labels never use.
extern const lv_font_t lv_font_montserrat_16_es;
extern const lv_font_t lv_font_montserrat_20_es;

#ifdef __cplusplus
}
#endif
