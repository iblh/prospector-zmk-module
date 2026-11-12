/* SPDX-License-Identifier: MIT */
#pragma once
#include <lvgl.h>
#include <fonts.h>

#define PSP_CONTROL_BG 0x0B1218
#define PSP_CONTROL_PANEL 0x111F28
#define PSP_CONTROL_LINE 0x2B4655
#define PSP_CONTROL_ACCENT 0x55D9D5
#define PSP_CONTROL_TEXT 0xDFEAF0
#define PSP_CONTROL_MUTED 0x879FAA

static inline void prospector_control_text(lv_obj_t *label) {
    lv_obj_set_style_text_font(label, &FG_Medium_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(PSP_CONTROL_TEXT), 0);
}
static inline void prospector_control_small(lv_obj_t *label) {
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(PSP_CONTROL_MUTED), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}
static inline void prospector_control_button(lv_obj_t *button) {
    lv_obj_set_style_bg_color(button, lv_color_hex(PSP_CONTROL_PANEL), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x20434E), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(PSP_CONTROL_LINE), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(PSP_CONTROL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 3, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
}
static inline void prospector_control_rule(lv_obj_t *parent) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 16, 36);
    lv_obj_set_size(line, 248, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(PSP_CONTROL_LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}
