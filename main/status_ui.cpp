#include "status_ui.h"
#include "ble_hid.h"

#include <cstring>
#include <cstdlib>
#include "lvgl.h"
#include "esp_lvgl_port.h"

namespace cr {

static lv_obj_t *s_ring;
static lv_obj_t *s_statusLabel;
static lv_obj_t *s_eventLabel;
static lv_obj_t *s_title;

/* one round action button */
static lv_obj_t *makeButton(lv_obj_t *parent, const char *label, uint32_t color,
                            int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 96, 96);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);
    return btn;
}

/* drag anywhere outside the buttons = scroll wheel */
static void scrollDragCb(lv_event_t *e)
{
    static lv_point_t lastPoint;
    static int accum = 0;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lastPoint = p;
        accum = 0;
        return;
    }
    accum += p.y - lastPoint.y;
    lastPoint = p;
    /* finger up = content down = wheel down (touchscreen-natural) */
    while (accum >= 28)  { bleHidScroll(-1); accum -= 28; statusUiSetEvent("scroll v"); }
    while (accum <= -28) { bleHidScroll(1); accum += 28; statusUiSetEvent("scroll ^"); }
}

struct StatusStyle {
    uint32_t color;
    const char *text;
};

static StatusStyle styleFor(ClaudeStatus st)
{
    switch (st) {
    case ST_BOOT:     return {0x4c6ef5, "STARTING"};
    case ST_NO_KBD:   return {0x555a66, "PLUG KEYBOARD"};
    case ST_READY:    return {0xf2f4f8, "READY"};
    case ST_RUNNING:  return {0x35e08a, "RUNNING"};
    case ST_QUESTION: return {0xffc53d, "CHOOSE"};
    case ST_STOPPED:  return {0xff2e3f, "STOPPED"};
    }
    return {0x555a66, "?"};
}

void statusUiCreate()
{
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0e1a), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "CLAUDE");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x8a8f9c), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 64);

    s_ring = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, 190, 190);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 14, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(0x4c6ef5), 0);
    lv_obj_center(s_ring);

    s_statusLabel = lv_label_create(scr);
    lv_label_set_text(s_statusLabel, "STARTING");
    lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_statusLabel, lv_color_hex(0xf2f4f8), 0);
    lv_obj_center(s_statusLabel);

    s_eventLabel = lv_label_create(scr);
    lv_label_set_text(s_eventLabel, "");
    lv_obj_set_style_text_color(s_eventLabel, lv_color_hex(0x8a8f9c), 0);
    lv_obj_align(s_eventLabel, LV_ALIGN_BOTTOM_MID, 0, -24);

    /* drag-to-scroll on the background */
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, scrollDragCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(scr, scrollDragCb, LV_EVENT_PRESSING, nullptr);

    /* action buttons along the bottom arc: ESC | MIC | ENTER */
    makeButton(scr, "ESC", 0xb3121f, 36, 258, [](lv_event_t *) {
        bleHidSendKey(KEY_ESC);
        statusUiSetEvent("ESC sent");
    });
    makeButton(scr, "MIC", 0x2456c9, 158, 292, [](lv_event_t *) {
        bleHidSendKey(KEY_F5);
        statusUiSetEvent("MIC (F5) sent");
    });
    makeButton(scr, "ENTER", 0x1d9e5a, 280, 258, [](lv_event_t *) {
        bleHidSendKey(KEY_ENTER);
        statusUiSetEvent("ENTER sent");
    });
    lvgl_port_unlock();
}

void statusUiSetStatus(ClaudeStatus st)
{
    StatusStyle s = styleFor(st);
    lvgl_port_lock(0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(s.color), 0);
    lv_label_set_text(s_statusLabel, s.text);
    lvgl_port_unlock();
}

void statusUiSetEvent(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_eventLabel, text);
    lvgl_port_unlock();
}

} // namespace cr
