#include "ble_hid.h"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "esp_log.h"

namespace cr {

static const char *TAG = "ble_hid";

static esp_hidd_dev_t *s_dev = nullptr;
static volatile bool s_connected = false;

/* keyboard (report id 1) + mouse with wheel (report id 2) */
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
};

static esp_hid_raw_report_map_t s_reportMaps[] = {
    {.data = REPORT_MAP, .len = sizeof(REPORT_MAP)},
};

static esp_hid_device_config_t s_hidConfig = {
    .vendor_id = 0x16C0,
    .product_id = 0x0C1D,
    .version = 0x0100,
    .device_name = "Claude Remote",
    .manufacturer_name = "Granoala",
    .serial_number = "1",
    .report_maps = s_reportMaps,
    .report_maps_len = 1,
};

static uint8_t s_hidServiceUuid[16] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00, /* 0x1812 HID */
};

static esp_ble_adv_params_t s_advParams = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void startAdvertising()
{
    esp_ble_adv_data_t advData = {};
    advData.set_scan_rsp = false;
    advData.include_name = true;
    advData.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    advData.flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
    advData.service_uuid_len = sizeof(s_hidServiceUuid);
    advData.p_service_uuid = s_hidServiceUuid;
    esp_ble_gap_config_adv_data(&advData);
}

static void gapCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&s_advParams);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "auth %s", param->ble_security.auth_cmpl.success ? "ok" : "FAILED");
        break;
    default:
        break;
    }
}

static void hiddCallback(void *, esp_event_base_t, int32_t id, void *event_data)
{
    esp_hidd_event_data_t *ev = (esp_hidd_event_data_t *)event_data;
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID started, advertising");
        startAdvertising();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        s_connected = true;
        ESP_LOGI(TAG, "host connected");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        ESP_LOGW(TAG, "host disconnected, re-advertising");
        startAdvertising();
        break;
    default:
        (void)ev;
        break;
    }
}

bool bleHidInit()
{
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&btCfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gapCallback));
    esp_ble_gap_set_device_name(s_hidConfig.device_name);

    /* Just Works bonding — what a screen-less HID remote can offer */
    esp_ble_auth_req_t authReq = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t ioCap = ESP_IO_CAP_NONE;
    uint8_t keySize = 16;
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rspKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(authReq));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ioCap, sizeof(ioCap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKey, sizeof(initKey));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKey, sizeof(rspKey));

    esp_err_t err = esp_hidd_dev_init(&s_hidConfig, ESP_HID_TRANSPORT_BLE, hiddCallback, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hidd init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "BLE HID up as '%s'", s_hidConfig.device_name);
    return true;
}

bool bleHidConnected() { return s_connected; }

void bleHidSendKey(uint8_t usage, uint8_t modifiers)
{
    if (!s_connected || !s_dev) return;
    uint8_t report[8] = {modifiers, 0, usage, 0, 0, 0, 0, 0};
    esp_hidd_dev_input_set(s_dev, 0, 1, report, sizeof(report));
    vTaskDelay(pdMS_TO_TICKS(15));
    memset(report, 0, sizeof(report));
    esp_hidd_dev_input_set(s_dev, 0, 1, report, sizeof(report));
}

void bleHidScroll(int8_t wheel)
{
    if (!s_connected || !s_dev) return;
    uint8_t report[4] = {0, 0, 0, (uint8_t)wheel};
    esp_hidd_dev_input_set(s_dev, 0, 2, report, sizeof(report));
}

} // namespace cr
