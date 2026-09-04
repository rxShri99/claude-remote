#include "mic_stream.h"
#include "ble_audio.h"

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

namespace cr {

static const char *TAG = "mic";

#define PIN_MIC_BCK GPIO_NUM_15
#define PIN_MIC_WS  GPIO_NUM_2
#define PIN_MIC_DIN GPIO_NUM_39

constexpr int SAMPLE_RATE = 16000;
constexpr int BLOCK_SAMPLES = 256; /* 16ms -> ~62 notifications/s, ~8.5kB/s */

static i2s_chan_handle_t s_rx = nullptr;
static TaskHandle_t s_task = nullptr;
static volatile bool s_run = false;
static volatile bool s_active = false;

/* ---- IMA ADPCM encoder (matches decoder in tools/claude_mic.py) ---- */

static const int STEP_TAB[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int INDEX_TAB[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct AdpcmState {
    int predictor = 0;
    int index = 0;
};

static uint8_t encodeSample(AdpcmState &st, int16_t sample)
{
    int step = STEP_TAB[st.index];
    int diff = sample - st.predictor;
    uint8_t code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; }
    if (diff >= step / 2) { code |= 2; diff -= step / 2; }
    if (diff >= step / 4) { code |= 1; }
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    st.predictor += (code & 8) ? -delta : delta;
    if (st.predictor > 32767) st.predictor = 32767;
    if (st.predictor < -32768) st.predictor = -32768;
    st.index += INDEX_TAB[code & 0x0F];
    if (st.index < 0) st.index = 0;
    if (st.index > 88) st.index = 88;
    return code;
}

/* ---- capture task ---- */

static void micTask(void *)
{
    static int32_t raw[BLOCK_SAMPLES];
    static int16_t pcm[BLOCK_SAMPLES];
    /* pkt: type, predictor lo/hi, index, rsv, nibbles */
    static uint8_t pkt[5 + BLOCK_SAMPLES / 2];
    AdpcmState st;

    uint8_t start = AUD_START;
    bleAudioNotify(&start, 1);

    while (s_run) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, raw, sizeof(raw), &got, 200) != ESP_OK ||
            got < sizeof(raw)) {
            continue;
        }
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            int32_t v = raw[i] >> 14; /* 24-bit MEMS data in 32-bit slot + gain */
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            pcm[i] = (int16_t)v;
        }
        /* self-contained block: header carries the codec state */
        pkt[0] = AUD_FRAME;
        pkt[1] = (uint8_t)(st.predictor & 0xFF);
        pkt[2] = (uint8_t)((st.predictor >> 8) & 0xFF);
        pkt[3] = (uint8_t)st.index;
        pkt[4] = 0;
        for (int i = 0; i < BLOCK_SAMPLES; i += 2) {
            uint8_t lo = encodeSample(st, pcm[i]);
            uint8_t hi = encodeSample(st, pcm[i + 1]);
            pkt[5 + i / 2] = (uint8_t)(lo | (hi << 4));
        }
        bleAudioNotify(pkt, sizeof(pkt));
    }

    s_task = nullptr;
    vTaskDelete(nullptr);
}

bool micStreamStart()
{
    if (s_active) return true;

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chanCfg, nullptr, &s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s channel alloc failed");
        return false;
    }
    i2s_std_config_t stdCfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_MIC_BCK,
            .ws = PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_MIC_DIN,
            .invert_flags = {},
        },
    };
    /* the MEMS mic drives the RIGHT slot on this board (per Waveshare's
       official MIC driver) — the default mono config reads the empty left
       slot and yields pure noise */
    stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    if (i2s_channel_init_std_mode(s_rx, &stdCfg) != ESP_OK ||
        i2s_channel_enable(s_rx) != ESP_OK) {
        ESP_LOGE(TAG, "i2s init failed");
        i2s_del_channel(s_rx);
        s_rx = nullptr;
        return false;
    }

    s_run = true;
    s_active = true;
    xTaskCreate(micTask, "mic", 4096, nullptr, 6, &s_task);
    ESP_LOGI(TAG, "mic streaming");
    return true;
}

void micStreamStop(bool send)
{
    if (!s_active) return;
    s_run = false;
    while (s_task) vTaskDelay(pdMS_TO_TICKS(10));
    i2s_channel_disable(s_rx);
    i2s_del_channel(s_rx);
    s_rx = nullptr;
    s_active = false;

    uint8_t end = send ? AUD_END_SEND : AUD_END_CANCEL;
    bleAudioNotify(&end, 1);
    ESP_LOGI(TAG, "mic stopped (%s)", send ? "send" : "cancel");
}

bool micStreamActive() { return s_active; }

} // namespace cr
