#pragma once
#include <cstdint>

namespace cr {

/* BLE HID keyboard+mouse device ("Claude Remote") for the Mac. */
bool bleHidInit();
bool bleHidConnected();

/* press+release of one key. usage = HID keyboard usage id (0x28 = Enter). */
void bleHidSendKey(uint8_t usage, uint8_t modifiers = 0);

/* mouse wheel: positive = scroll up */
void bleHidScroll(int8_t wheel);

/* common usages */
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_ESC = 0x29;
constexpr uint8_t KEY_F5 = 0x3E;   /* macOS dictation (set in System Settings) */
constexpr uint8_t KEY_UP = 0x52;
constexpr uint8_t KEY_DOWN = 0x51;

} // namespace cr
