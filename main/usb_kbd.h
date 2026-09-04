#pragma once

namespace cr {

/*
 * USB host for the CH57x 3-key + knob macro pad. Once this starts, the
 * board's USB port belongs to the keyboard — serial console and flashing
 * over USB are gone until you re-enter download mode (hold BOOT, replug).
 */
using KbdEventCb = void (*)(const char *text); /* human-readable key event */
using KbdStateCb = void (*)(bool connected);

bool usbKbdInit(KbdEventCb onEvent, KbdStateCb onState);

} // namespace cr
