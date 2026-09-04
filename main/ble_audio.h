#pragma once
#include <cstdint>
#include <cstddef>

namespace cr {

/*
 * Custom GATT service carrying compressed mic audio to the Mac helper
 * (tools/claude_mic.py), alongside the HID service. Notification payloads:
 *   0x01                 stream start (16kHz IMA-ADPCM mono)
 *   0x03 <adpcm block>   audio: int16 predictor, uint8 index, uint8 rsv, nibbles
 *   0x02                 stream end -> transcribe, type, press Enter
 *   0x04                 stream end -> discard (cancel)
 */
constexpr uint8_t AUD_START = 0x01;
constexpr uint8_t AUD_END_SEND = 0x02;
constexpr uint8_t AUD_FRAME = 0x03;
constexpr uint8_t AUD_END_CANCEL = 0x04;

/* call INSTEAD of registering esp_hidd's GATTS callback: wraps it */
bool bleAudioRegisterGatts();
/* register the audio GATT app; call after esp_hidd_dev_init */
bool bleAudioStart();

bool bleAudioReady(); /* helper subscribed */
bool bleAudioNotify(const uint8_t *data, size_t len);

} // namespace cr
