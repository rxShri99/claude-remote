# Claude Remote

A touch + Bluetooth controller for Claude Code: the Waveshare
ESP32-S3-Touch-LCD-1.46's round touchscreen is the control surface, and the
board pairs with the Mac as a BLE HID keyboard+mouse named **Claude Remote**.

## Controls

| Input | Action |
|---|---|
| Green ENTER button | Enter (submit / confirm) |
| Blue MIC button | F5 (set macOS dictation shortcut to F5) |
| Red ESC button | Esc (stop Claude) |
| Drag anywhere else | Scroll (mouse wheel, touchscreen-natural) |

Status ring: blue starting · grey pair-me · white ready · green
running/thinking · amber question · red stopped (Claude states arrive via
Claude Code hooks in a later phase).

## Architecture

```
round touchscreen --> ESP32-S3 --BLE HID--> Mac (Claude Code)
      ^ status ring                            |
      '------- BLE GATT status <-- Claude Code hooks (planned)
```

- Touch: SPD2010 driver ported in-repo to the new `i2c_master` API
  ([touch_spd2010.cpp](main/touch_spd2010.cpp)) — the upstream component's
  I/O layer is incompatible with it.
- BLE: Bluedroid, Just Works bonding, keyboard + mouse-wheel report map,
  auto re-advertises on disconnect.

## Phases

- [x] P1 — display + touch + button UI
- [x] P2 — BLE HID pairing and Enter/Esc/F5/scroll forwarding
- [ ] P3 — question mode: drag = Up/Down between options, tap = select
- [ ] P4 — BLE status service + Claude Code hooks (Mac scripts in tools/)
      driving the ring colors

## Hardware gotchas (hard-won on this board family)

- **Battery power latch**: firmware holds GPIO7 high; hold PWR ~1s to boot on
  battery, ~3s to power off (a true cold cycle — also clears a latched panel).
- **Display latch-up**: dark screen while the board runs = SPD2010 latched by a
  power transition; remove ALL power (USB + battery) for 10s.
- **Download-mode trap**: holding BOOT at plug-in sets a *persistent*
  force-download flag — the app won't boot (black screen) until a true
  power-on reset or `esptool write_mem 0x6000812C 0`. Avoid serial scripts
  that toggle DTR/RTS on this firmware; listen passively instead.

## Build & flash

ESP-IDF v5.5.2 at `../esp-idf`:

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXX flash
```
