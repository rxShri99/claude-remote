# Claude Remote

A physical controller for Claude Code: Waveshare ESP32-S3-Touch-LCD-1.46 as a
**USB host** for a CH57x 3-key + knob macro pad, relaying input to the Mac as a
**BLE HID** device, with Claude's live status on the round display.

## Controls (target)

| Input | Action |
|---|---|
| Right key | Enter (submit / confirm) |
| Middle key | Mic (dictation hotkey) |
| Left key | Esc (stop Claude) |
| Knob turn | Scroll (mouse wheel); in question mode: Up/Down between options |
| Knob press | Enter (select option) |

Display ring: blue starting · grey no keyboard · white ready · green
running/thinking · amber question (knob = select) · red stopped.

## Architecture

```
CH57x keypad --USB--> ESP32-S3 (host) --BLE HID--> Mac (Claude Code)
                          ^  display: status ring       |
                          '---- BLE GATT status <-- Claude Code hooks
```

## Phases

- [x] P1 — USB host: enumerate the CH57x, show every key/knob event on screen
- [ ] P2 — BLE HID keyboard+mouse to the Mac; forward Enter/Esc/wheel
- [ ] P3 — modes: scroll vs question-select; mic hotkey mapping
- [ ] P4 — BLE status service + Claude Code hooks (Mac scripts in tools/) → ring colors

## Hardware gotchas (inherited from Tether — see that repo's history)

- **VBUS**: this board cannot source 5V on its USB-C. The keyboard likely needs
  a powered OTG adapter/Y-cable, with the board itself on battery.
- **Flashing**: once USB host mode owns the port, serial/flash over USB is
  dead. To reflash: hold BOOT, replug USB, then `idf.py flash`. The display is
  the debug console (bottom line shows raw HID events).
- **Battery power latch**: GPIO7 held high in firmware; hold PWR ~1s to boot on
  battery, ~3s to power off (also clears a latched display).
- **Display latch-up**: if the screen is dark but the board runs, remove ALL
  power (USB + battery) for 10s.

## Keyboard setup (one-time, on the Mac)

Program distinct keys with [ch57x-keyboard-tool](https://github.com/kriomant/ch57x-keyboard-tool)
so the firmware can map unambiguously — suggested: left=F13, mid=F14,
right=F15, knob ccw/cw/press = F16/F17/F18 (config in tools/ later).

## Build & flash

ESP-IDF v5.5.2 at `../esp-idf`:

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXX flash   # hold BOOT + replug first if host fw is on
```
