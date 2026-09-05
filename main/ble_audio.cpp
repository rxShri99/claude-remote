#include "ble_audio.h"

#include <cstring>
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "status_ui.h"
#include <cstdio>

namespace cr {

static const char *TAG = "ble_audio";

constexpr uint16_t AUDIO_APP_ID = 0x0A0D; /* must be <= 0x7FFF */

/*
 * Canonical UUIDs (mirrored in tools/claude_mic.py):
 *   service:        7a0d0001-c1a0-de50-b3a4-4b1d4e101001
 *   characteristic: 7a0d0002-c1a0-de50-b3a4-4b1d4e101001
 * Byte arrays below are the same values in BLE little-endian order.
 */
static const uint8_t SVC_UUID[16] = {
    0x01, 0x10, 0x10, 0x4e, 0x1d, 0x4b, 0xa4, 0xb3,
    0x50, 0xde, 0xa0, 0xc1, 0x01, 0x00, 0x0d, 0x7a,
};
static const uint8_t CHR_UUID[16] = {
    0x01, 0x10, 0x10, 0x4e, 0x1d, 0x4b, 0xa4, 0xb3,
    0x50, 0xde, 0xa0, 0xc1, 0x02, 0x00, 0x0d, 0x7a,
};
/* text-back characteristic 7a0d0003-...: Mac writes transcripts/responses */
static const uint8_t TXT_UUID[16] = {
    0x01, 0x10, 0x10, 0x4e, 0x1d, 0x4b, 0xa4, 0xb3,
    0x50, 0xde, 0xa0, 0xc1, 0x03, 0x00, 0x0d, 0x7a,
};

enum { IDX_SVC, IDX_CHAR_DECL, IDX_CHAR_VAL, IDX_CCC, IDX_TXT_DECL, IDX_TXT_VAL, IDX_NB };

static const uint16_t UUID_PRI_SVC = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t UUID_CHAR_DECL = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t UUID_CCC = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t CHAR_PROP_NOTIFY = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t CHAR_PROP_WRITE = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static uint8_t s_charVal[1] = {0};
static uint8_t s_txtVal[1] = {0};
static uint8_t s_cccVal[2] = {0, 0};

static const esp_gatts_attr_db_t ATTR_TAB[IDX_NB] = {
    /* IDX_SVC */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *)&UUID_PRI_SVC, ESP_GATT_PERM_READ,
      sizeof(SVC_UUID), sizeof(SVC_UUID), (uint8_t *)SVC_UUID}},
    /* IDX_CHAR_DECL */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *)&UUID_CHAR_DECL, ESP_GATT_PERM_READ,
      1, 1, (uint8_t *)&CHAR_PROP_NOTIFY}},
    /* IDX_CHAR_VAL */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_128, (uint8_t *)CHR_UUID, ESP_GATT_PERM_READ,
      512, sizeof(s_charVal), s_charVal}},
    /* IDX_CCC */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *)&UUID_CCC, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
      sizeof(s_cccVal), sizeof(s_cccVal), s_cccVal}},
    /* IDX_TXT_DECL */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_16, (uint8_t *)&UUID_CHAR_DECL, ESP_GATT_PERM_READ,
      1, 1, (uint8_t *)&CHAR_PROP_WRITE}},
    /* IDX_TXT_VAL */
    {{ESP_GATT_AUTO_RSP},
     {ESP_UUID_LEN_128, (uint8_t *)TXT_UUID, ESP_GATT_PERM_WRITE,
      512, sizeof(s_txtVal), s_txtVal}},
};

/* ---- text-back protocol: 0x20 start, 0x21 append, 0x22 show; 0x30+state ---- */
static char s_textBuf[600];
static size_t s_textLen = 0;

static void onTextWrite(const uint8_t *data, uint16_t len)
{
    uint8_t op = data[0];
    if (op == 0x30 && len >= 2) {
        switch (data[1]) {
        case 1: statusUiSetStatus(ST_RUNNING); break;
        case 2: statusUiSetStatus(ST_QUESTION); break;
        case 3: statusUiSetStatus(ST_STOPPED); break;
        default: statusUiSetStatus(ST_READY); break;
        }
        return;
    }
    if (op == 0x20) s_textLen = 0;
    if (op == 0x20 || op == 0x21) {
        size_t n = len - 1;
        if (s_textLen + n >= sizeof(s_textBuf)) n = sizeof(s_textBuf) - 1 - s_textLen;
        memcpy(s_textBuf + s_textLen, data + 1, n);
        s_textLen += n;
    } else if (op == 0x22) {
        s_textBuf[s_textLen] = 0;
        statusUiShowResponse(s_textBuf);
    }
}

static esp_gatt_if_t s_if = ESP_GATT_IF_NONE;
static uint16_t s_handles[IDX_NB] = {};
static volatile uint16_t s_connId = 0xFFFF;
static volatile bool s_subscribed = false;

static void gattsCb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                    esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT && param->reg.app_id == AUDIO_APP_ID) {
        s_if = gatts_if;
        ESP_LOGI(TAG, "REG_EVT for audio app, if=%d", gatts_if);
        esp_err_t err = esp_ble_gatts_create_attr_tab(ATTR_TAB, gatts_if, IDX_NB, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "create_attr_tab: %s", esp_err_to_name(err));
            char msg[48];
            snprintf(msg, sizeof(msg), "aud tab call err %d", err);
            statusUiSetEvent(msg);
        }
        return; /* ours alone */
    }

    if (s_if != ESP_GATT_IF_NONE && (gatts_if == s_if || gatts_if == ESP_GATT_IF_NONE)) {
        switch (event) {
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == IDX_NB) {
                memcpy(s_handles, param->add_attr_tab.handles, sizeof(s_handles));
                esp_ble_gatts_start_service(s_handles[IDX_SVC]);
                ESP_LOGI(TAG, "audio service started (val handle %d)", s_handles[IDX_CHAR_VAL]);
                statusUiSetEvent("audio svc up");
            } else {
                char msg[48];
                snprintf(msg, sizeof(msg), "aud tab st=%d n=%d", param->add_attr_tab.status,
                         param->add_attr_tab.num_handle);
                statusUiSetEvent(msg);
            }
            break;
        case ESP_GATTS_CONNECT_EVT:
            s_connId = param->connect.conn_id;
            s_subscribed = false;
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            s_connId = 0xFFFF;
            s_subscribed = false;
            break;
        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == s_handles[IDX_CCC] && param->write.len >= 2) {
                s_subscribed = (param->write.value[0] & 0x01) != 0;
                ESP_LOGI(TAG, "helper %ssubscribed", s_subscribed ? "" : "un");
            } else if (param->write.handle == s_handles[IDX_TXT_VAL] && param->write.len >= 1) {
                onTextWrite(param->write.value, param->write.len);
            }
            break;
        default:
            break;
        }
        if (gatts_if == s_if) {
            return; /* fully handled */
        }
    }

    /* everything else belongs to the HID service */
    esp_hidd_gatts_event_handler(event, gatts_if, param);
}

bool bleAudioRegisterGatts()
{
    return esp_ble_gatts_register_callback(gattsCb) == ESP_OK;
}

bool bleAudioStart()
{
    esp_err_t err = esp_ble_gatts_app_register(AUDIO_APP_ID);
    ESP_LOGI(TAG, "app_register(0x%04X) -> %s", AUDIO_APP_ID, esp_err_to_name(err));
    if (err != ESP_OK) {
        char msg[48];
        snprintf(msg, sizeof(msg), "aud app_reg err %d", err);
        statusUiSetEvent(msg);
    }
    return err == ESP_OK;
}

bool bleAudioReady()
{
    return s_subscribed && s_connId != 0xFFFF;
}

bool bleAudioNotify(const uint8_t *data, size_t len)
{
    if (!bleAudioReady()) return false;
    return esp_ble_gatts_send_indicate(s_if, s_connId, s_handles[IDX_CHAR_VAL],
                                       len, (uint8_t *)data, false) == ESP_OK;
}

} // namespace cr
