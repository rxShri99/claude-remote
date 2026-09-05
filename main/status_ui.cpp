#include "status_ui.h"
#include "ble_hid.h"
#include "mic_stream.h"
#include "ble_audio.h"

#include <cstring>
#include "lvgl.h"
#include "esp_lvgl_port.h"

namespace cr {

/* ---- onboarding screen ---- */
static lv_obj_t *s_pairScreen;
static lv_obj_t *s_pairIcon;
static lv_obj_t *s_pairStatus;

/* ---- remote screen ---- */
static lv_obj_t *s_remoteScreen;
static lv_obj_t *s_statusPill;
static lv_obj_t *s_statusLabel;
static lv_obj_t *s_eventLabel;
static lv_obj_t *s_responseLabel;

/* ---- mic (dictation) screen ---- */
static lv_obj_t *s_micScreen;
static lv_obj_t *s_micStatus;

static void micScreenShow(bool show)
{
    if (show) {
        lv_obj_clear_flag(s_micScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_remoteScreen, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_micScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_remoteScreen, LV_OBJ_FLAG_HIDDEN);
    }
}

struct StatusStyle {
    uint32_t color;
    const char *text;
};

static StatusStyle styleFor(ClaudeStatus st)
{
    switch (st) {
    case ST_READY:    return {0xf2f4f8, "READY"};
    case ST_RUNNING:  return {0x35e08a, "RUNNING"};
    case ST_QUESTION: return {0xffc53d, "CHOOSE"};
    case ST_STOPPED:  return {0xff2e3f, "STOPPED"};
    case ST_THINKING: return {0xb08ae8, "THINKING"};
    case ST_WORKING:  return {0x2ec4b6, "WORKING"};
    }
    return {0x555a66, "?"};
}

static lv_obj_t *fullscreenPanel(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0b0e1a), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* ------------------------------------------------ onboarding screen */

static void buildPairScreen(lv_obj_t *parent)
{
    s_pairScreen = fullscreenPanel(parent);

    lv_obj_t *title = lv_label_create(s_pairScreen);
    lv_label_set_text(title, "CLAUDE REMOTE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xf2f4f8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 78);

    /* bluetooth badge with a gentle breathe (small area — repaint is cheap) */
    lv_obj_t *badge = lv_obj_create(s_pairScreen);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 132, 132);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(badge, 6, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0x2f9bff), 0);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -30);

    s_pairIcon = lv_label_create(badge);
    lv_label_set_text(s_pairIcon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(s_pairIcon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_pairIcon, lv_color_hex(0x2f9bff), 0);
    lv_obj_center(s_pairIcon);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pairIcon);
    lv_anim_set_exec_cb(&a, [](void *var, int32_t v) {
        lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
    });
    lv_anim_set_values(&a, 255, 70);
    lv_anim_set_duration(&a, 1100);
    lv_anim_set_playback_duration(&a, 1100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    s_pairStatus = lv_label_create(s_pairScreen);
    lv_label_set_text(s_pairStatus, "starting bluetooth...");
    lv_obj_set_style_text_color(s_pairStatus, lv_color_hex(0x8bd3ff), 0);
    lv_obj_align(s_pairStatus, LV_ALIGN_CENTER, 0, 66);

    lv_obj_t *steps = lv_label_create(s_pairScreen);
    lv_label_set_text(steps, "on your Mac:\nSettings > Bluetooth\nconnect \"Claude Remote\"");
    lv_obj_set_style_text_font(steps, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(steps, lv_color_hex(0x8a8f9c), 0);
    lv_obj_set_style_text_align(steps, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(steps, LV_ALIGN_BOTTOM_MID, 0, -64);
}

/* ------------------------------------------------ remote screen */

static lv_obj_t *makeButton(lv_obj_t *parent, const char *label, uint32_t color,
                            int x, int y, int size, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, size >= 100 ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0);
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

static void buildRemoteScreen(lv_obj_t *parent)
{
    s_remoteScreen = fullscreenPanel(parent);
    lv_obj_add_flag(s_remoteScreen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_remoteScreen);
    lv_label_set_text(title, "CLAUDE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8a8f9c), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

    /* status pill: color = Claude state */
    s_statusPill = lv_obj_create(s_remoteScreen);
    lv_obj_remove_style_all(s_statusPill);
    lv_obj_set_size(s_statusPill, 168, 46);
    lv_obj_set_style_radius(s_statusPill, 23, 0);
    lv_obj_set_style_bg_opa(s_statusPill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_statusPill, lv_color_hex(0x2a3040), 0);
    lv_obj_align(s_statusPill, LV_ALIGN_TOP_MID, 0, 76);

    s_statusLabel = lv_label_create(s_statusPill);
    lv_label_set_text(s_statusLabel, "READY");
    lv_obj_set_style_text_font(s_statusLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_statusLabel, lv_color_hex(0x0b0e1a), 0);
    lv_obj_center(s_statusLabel);

    /* conversation window: transcripts + Claude's replies land here */
    s_responseLabel = lv_label_create(s_remoteScreen);
    lv_label_set_text(s_responseLabel, "");
    lv_label_set_long_mode(s_responseLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_responseLabel, 312);
    lv_obj_set_height(s_responseLabel, 92); /* ~4 lines, ellipsized */
    lv_obj_set_style_text_font(s_responseLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_responseLabel, lv_color_hex(0xd7dbe4), 0);
    lv_obj_set_style_text_align(s_responseLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_responseLabel, LV_ALIGN_TOP_MID, 0, 130);

    s_eventLabel = lv_label_create(s_remoteScreen);
    lv_label_set_text(s_eventLabel, "drag to scroll");
    lv_obj_set_style_text_color(s_eventLabel, lv_color_hex(0x8a8f9c), 0);
    lv_obj_align(s_eventLabel, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* drag-to-scroll on the background */
    lv_obj_add_flag(s_remoteScreen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_remoteScreen, scrollDragCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_remoteScreen, scrollDragCb, LV_EVENT_PRESSING, nullptr);

    /* three BIG action buttons: ESC | MIC | ENTER (112px, sized to fit
       the round bezel: every button center stays within r=150 of screen
       center so the full circle is visible) */
    makeButton(s_remoteScreen, "ESC", 0xb3121f, 34, 226, 112, [](lv_event_t *) {
        bleHidSendKey(KEY_ESC);
        statusUiSetStatus(ST_STOPPED); /* instant feedback; watcher corrects later */
        statusUiSetEvent("ESC sent");
    });
    makeButton(s_remoteScreen, "MIC", 0x2456c9, 150, 246, 112, [](lv_event_t *) {
        if (!bleAudioReady()) {
            statusUiSetEvent("start claude_mic.py on the Mac");
            return;
        }
        micStreamStart();
        micScreenShow(true);
    });
    makeButton(s_remoteScreen, "ENTER", 0x1d9e5a, 266, 226, 112, [](lv_event_t *) {
        bleHidSendKey(KEY_ENTER);
        statusUiSetEvent("ENTER sent");
    });

    /* small disconnect button, top-right */
    lv_obj_t *dc = makeButton(s_remoteScreen, LV_SYMBOL_CLOSE, 0x3a3f4d, 296, 66, 48, [](lv_event_t *) {
        statusUiSetEvent("disconnecting...");
        bleHidDisconnect();
    });
    lv_obj_set_style_text_color(lv_obj_get_child(dc, 0), lv_color_hex(0xff8a8a), 0);
}

/* ------------------------------------------------ mic screen */

static void buildMicScreen(lv_obj_t *parent)
{
    s_micScreen = fullscreenPanel(parent);
    lv_obj_set_style_bg_color(s_micScreen, lv_color_hex(0x14060a), 0);
    lv_obj_add_flag(s_micScreen, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_micScreen);
    lv_label_set_text(title, "LISTENING");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff5a5f), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);

    /* pulsing record dot */
    lv_obj_t *dot = lv_obj_create(s_micScreen);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 84, 84);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xff2e3f), 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, -66);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, [](void *var, int32_t v) {
        lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
    });
    lv_anim_set_values(&a, 255, 90);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_playback_duration(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    s_micStatus = lv_label_create(s_micScreen);
    lv_label_set_text(s_micStatus, "speak into the device");
    lv_obj_set_style_text_color(s_micStatus, lv_color_hex(0xffb3b8), 0);
    lv_obj_align(s_micStatus, LV_ALIGN_CENTER, 0, 4);

    /* CANCEL | SEND */
    makeButton(s_micScreen, "CANCEL", 0x3a3f4d, 52, 236, 112, [](lv_event_t *) {
        micStreamStop(false);
        micScreenShow(false);
        statusUiSetEvent("voice cancelled");
    });
    makeButton(s_micScreen, "SEND", 0x1d9e5a, 248, 236, 112, [](lv_event_t *) {
        micStreamStop(true); /* helper transcribes, types, presses Enter */
        micScreenShow(false);
        statusUiSetEvent("transcribing on Mac...");
    });
}

/* ------------------------------------------------ api */

void statusUiCreate()
{
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0e1a), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    buildPairScreen(scr);
    buildRemoteScreen(scr);
    buildMicScreen(scr);
    lvgl_port_unlock();
}

void statusUiSetConnected(bool connected)
{
    lvgl_port_lock(0);
    if (connected) {
        lv_obj_add_flag(s_pairScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_remoteScreen, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_pairScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_remoteScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_micScreen, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

void statusUiSetStatus(ClaudeStatus st)
{
    StatusStyle s = styleFor(st);
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(s_statusPill, lv_color_hex(s.color), 0);
    lv_label_set_text(s_statusLabel, s.text);
    lvgl_port_unlock();
}

void statusUiShowResponse(const char *text)
{
    lvgl_port_lock(0);
    if (s_responseLabel) lv_label_set_text(s_responseLabel, text);
    lvgl_port_unlock();
}

void statusUiSetEvent(const char *text)
{
    lvgl_port_lock(0);
    if (s_pairStatus) lv_label_set_text(s_pairStatus, text);
    if (s_eventLabel) lv_label_set_text(s_eventLabel, text);
    lvgl_port_unlock();
}

} // namespace cr
