#include "notification_icon.h"
#include "notification_store.h"
#include "app_icons_data.h"

#include <cstring>

// Matched against the prettified app_name notification_store stores (the
// last dot-separated component of the ANCS AppIdentifier bundle ID — see
// prettify_app_name() in ancs_client.cpp), not the raw bundle ID.
static const lv_img_dsc_t *known_app_icon(const char *app_name)
{
    if (strcmp(app_name, "WhatsApp") == 0) return &img_app_whatsapp_36;
    if (strcmp(app_name, "Gmail") == 0)    return &img_app_gmail_36;
    return nullptr;
}

// Brand icon assets are baked at 36px (see app_icons_data.c) — other sizes
// are produced by zooming rather than generating another asset per size.
#define BRAND_ICON_NATIVE_SIZE 36

lv_obj_t *notification_icon_create(lv_obj_t *parent, const char *app_name, lv_coord_t size)
{
    const lv_img_dsc_t *icon = known_app_icon(app_name);
    if (icon) {
        lv_obj_t *img = lv_img_create(parent);
        lv_img_set_src(img, icon);
        if (size != BRAND_ICON_NATIVE_SIZE)
            lv_img_set_zoom(img, (uint16_t)(256 * size / BRAND_ICON_NATIVE_SIZE));
        return img;
    }

    lv_obj_t *avatar = lv_obj_create(parent);
    lv_obj_remove_style_all(avatar);
    lv_obj_clear_flag(avatar, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_size(avatar, size, size);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    uint8_t r, g, b;
    notification_avatar_rgb(app_name, &r, &g, &b);
    lv_obj_set_style_bg_color(avatar, lv_color_make(r, g, b), 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_set_layout(avatar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(avatar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char initials[3];
    notification_initials(app_name, initials, sizeof(initials));
    lv_obj_t *lbl = lv_label_create(avatar);
    lv_label_set_text(lbl, initials);
    lv_obj_set_style_text_font(lbl, size < 32 ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    return avatar;
}
