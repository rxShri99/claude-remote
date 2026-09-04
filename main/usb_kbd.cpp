#include "usb_kbd.h"

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "esp_log.h"

namespace cr {

static const char *TAG = "usb_kbd";

static KbdEventCb s_onEvent = nullptr;
static KbdStateCb s_onState = nullptr;
static QueueHandle_t s_devQueue = nullptr;

struct DevEvent {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
};

/* minimal HID usage names for the keys we expect the CH57x to send */
static const char *keyName(uint8_t usage)
{
    static char buf[8];
    if (usage >= 0x04 && usage <= 0x1D) { /* a-z */
        snprintf(buf, sizeof(buf), "%c", 'A' + usage - 0x04);
        return buf;
    }
    if (usage >= 0x1E && usage <= 0x27) { /* 1-0 */
        snprintf(buf, sizeof(buf), "%c", usage == 0x27 ? '0' : '1' + usage - 0x1E);
        return buf;
    }
    if (usage >= 0x3A && usage <= 0x45) { /* F1-F12 */
        snprintf(buf, sizeof(buf), "F%d", usage - 0x3A + 1);
        return buf;
    }
    if (usage >= 0x68 && usage <= 0x73) { /* F13-F24 */
        snprintf(buf, sizeof(buf), "F%d", usage - 0x68 + 13);
        return buf;
    }
    switch (usage) {
    case 0x28: return "ENTER";
    case 0x29: return "ESC";
    case 0x2C: return "SPACE";
    case 0x4F: return "RIGHT";
    case 0x50: return "LEFT";
    case 0x51: return "DOWN";
    case 0x52: return "UP";
    }
    snprintf(buf, sizeof(buf), "0x%02X", usage);
    return buf;
}

static void handleInputReport(hid_host_device_handle_t dev)
{
    uint8_t data[64];
    size_t len = 0;
    if (hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &len) != ESP_OK || len == 0) {
        return;
    }

    hid_host_dev_params_t params;
    hid_host_device_get_params(dev, &params);

    char text[64];
    if (params.proto == HID_PROTOCOL_KEYBOARD && len >= 8) {
        /* boot keyboard report: [mods, resv, key1..key6] */
        if (data[2] == 0) {
            return; /* key release — quiet */
        }
        int n = snprintf(text, sizeof(text), "KEY %s", keyName(data[2]));
        if (data[0]) snprintf(text + n, sizeof(text) - n, " mod%02X", data[0]);
    } else if (params.proto == HID_PROTOCOL_MOUSE && len >= 4) {
        /* boot mouse: [buttons, dx, dy, wheel] — the knob usually lands here */
        int8_t wheel = (int8_t)data[3];
        if (data[0]) snprintf(text, sizeof(text), "KNOB PRESS (btn%02X)", data[0]);
        else if (wheel) snprintf(text, sizeof(text), "KNOB %s (%d)", wheel > 0 ? "CW" : "CCW", wheel);
        else return;
    } else {
        /* anything else (consumer control etc.): dump raw so we learn the format */
        int n = snprintf(text, sizeof(text), "RAW[%u]", (unsigned)len);
        for (size_t i = 0; i < len && i < 8; i++) {
            n += snprintf(text + n, sizeof(text) - n, " %02X", data[i]);
        }
    }

    ESP_LOGI(TAG, "%s", text);
    if (s_onEvent) s_onEvent(text);
}

static void interfaceCallback(hid_host_device_handle_t dev, const hid_host_interface_event_t event, void *)
{
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        handleInputReport(dev);
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        hid_host_device_close(dev);
        ESP_LOGW(TAG, "keyboard disconnected");
        if (s_onState) s_onState(false);
        break;
    default:
        break;
    }
}

static void deviceCallback(hid_host_device_handle_t handle, const hid_host_driver_event_t event, void *)
{
    DevEvent ev = {handle, event};
    xQueueSend(s_devQueue, &ev, 0);
}

static void devHandlerTask(void *)
{
    DevEvent ev;
    while (true) {
        if (xQueueReceive(s_devQueue, &ev, portMAX_DELAY) != pdTRUE) continue;
        if (ev.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
            hid_host_dev_params_t params;
            hid_host_device_get_params(ev.handle, &params);
            ESP_LOGI(TAG, "HID connected: proto=%d subclass=%d", params.proto, params.sub_class);

            const hid_host_device_config_t devCfg = {
                .callback = interfaceCallback,
                .callback_arg = nullptr,
            };
            if (hid_host_device_open(ev.handle, &devCfg) == ESP_OK) {
                if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                    hid_class_request_set_protocol(ev.handle, HID_REPORT_PROTOCOL_BOOT);
                    if (params.proto == HID_PROTOCOL_KEYBOARD) {
                        hid_class_request_set_idle(ev.handle, 0, 0);
                    }
                }
                hid_host_device_start(ev.handle);
                if (s_onState) s_onState(true);
            }
        }
    }
}

static void usbLibTask(void *)
{
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

bool usbKbdInit(KbdEventCb onEvent, KbdStateCb onState)
{
    s_onEvent = onEvent;
    s_onState = onState;
    s_devQueue = xQueueCreate(8, sizeof(DevEvent));

    const usb_host_config_t hostCfg = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
        .fifo_settings_custom = {},
        .peripheral_map = 0,
    };
    esp_err_t err = usb_host_install(&hostCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
        return false;
    }
    xTaskCreate(usbLibTask, "usb_lib", 4096, nullptr, 10, nullptr);
    xTaskCreate(devHandlerTask, "hid_dev", 4096, nullptr, 9, nullptr);

    const hid_host_driver_config_t hidCfg = {
        .create_background_task = true,
        .task_priority = 10,
        .stack_size = 4096,
        .core_id = 0,
        .callback = deviceCallback,
        .callback_arg = nullptr,
    };
    err = hid_host_install(&hidCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "USB host up — waiting for the keyboard");
    return true;
}

} // namespace cr
