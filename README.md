# Claude Remote

A palm-sized, battery-powered hardware companion for [Claude Code](https://claude.com/claude-code):
speak to Claude from across the room, watch it think in color, read its replies,
and control it with touch — all from a round 1.46" screen on your desk.

```text
        you speak ──► onboard mic ──► ADPCM ──► BLE ──► Mac bridge ──► whisper.cpp
                                                                          │
     Cursor / Claude Code  ◄── keystrokes (text + Enter) ◄────────────────┘
            │
            └── transcript watcher ──► BLE ──► device shows Claude's state + replies
```

Everything is local: on-device audio capture, on-Mac speech-to-text, no cloud
services in the loop.

## Features

| On the device | What it does |
| --- | --- |
| 🎤 MIC button | records from the onboard mic; SEND transcribes locally and submits to Claude, CANCEL discards |
| 🟢 ENTER / 🔴 ESC | confirm / interrupt Claude (ESC also flips the ring red instantly) |
| 👆 drag anywhere | scrolls the Mac (mouse wheel) |
| 💬 message window | shows "You: …" transcripts and "Claude: …" replies |
| 💡 status pill | white READY · green RUNNING · purple THINKING · teal WORKING · red STOPPED |
| 📶 onboarding screen | pairing instructions + live radio status until the Mac connects |
| ✕ button | drops the BLE link (re-advertises for pairing) |
| 🔋 PWR button | ~1s hold boots on battery, ~3s hold powers off |

## Hardware

- **Waveshare ESP32-S3-Touch-LCD-1.46**: SPD2010 412×412 round touch LCD (QSPI),
  onboard I2S MEMS microphone, 16MB flash, 8MB PSRAM, LiPo support (MX1.25).
- A Mac with Bluetooth (the bridge + whisper.cpp run there).

## Quick start

### 1. Flash the firmware

ESP-IDF v5.5.2 (checkout expected at `../esp-idf`):

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXX flash
# then UNPLUG AND REPLUG USB — see "Download-mode trap" below
```

### 2. Mac-side toolchain (one-time)

```sh
brew install whisper-cpp
python3 -m venv tools/venv && tools/venv/bin/pip install bleak
curl -L -o tools/models/ggml-base.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
```

### 3. Pair

System Settings → Bluetooth → connect **"Claude Remote"** (no PIN; it shows up
as a keyboard). The device's onboarding screen walks you through it and flips
to the remote UI on connect.

### 4. Run the bridge

As a background service (recommended — starts at login, auto-restarts,
reconnects with backoff):

```sh
tools/install_service.sh
tail -f ~/Library/Logs/claude-remote.log
```

Or manually from **Terminal.app** (not an IDE terminal — see TCC below):

```sh
tools/venv/bin/python tools/claude_mic.py
```

First run: approve the **Bluetooth** prompt, and grant **Accessibility** when
the first voice message is typed.

### 5. Use it

Focus Claude's chat input, tap **MIC**, speak into the device, tap **SEND**.
Your words appear in the input and submit; the ring narrates Claude's state;
the reply text lands on the device.

> The bridge types wherever the keyboard focus is — click into Claude's input
> before sending.

## Architecture

### Firmware (`main/`)

| Module | Role |
| --- | --- |
| `display.cpp` | SPD2010 panel over QSPI + LVGL 9, battery power latch (GPIO7), TCA9554 expander resets |
| `touch_spd2010.cpp` | SPD2010 touch, ported to the new `i2c_master` API (upstream component's I/O layer is incompatible) |
| `status_ui.cpp` | three screens: onboarding, remote (buttons/pill/messages), mic (LISTENING + SEND/CANCEL) |
| `ble_hid.cpp` | BLE HID keyboard+mouse (Bluedroid, Just Works bonding); advertises even while connected |
| `ble_audio.cpp` | custom GATT service beside HID; wraps Bluedroid's single GATTS callback and forwards the rest to `esp_hidd` |
| `mic_stream.cpp` | I2S capture 16kHz mono (**right slot!**) → IMA-ADPCM blocks → BLE notifications (~8.5kB/s) |

### BLE protocol (service `7a0d0001-c1a0-de50-b3a4-4b1d4e101001`)

**Audio characteristic `…0002` (notify, device → Mac):**

| First byte | Meaning |
| --- | --- |
| `0x01` | stream start (16kHz IMA-ADPCM mono) |
| `0x03` | audio block: `int16 predictor, uint8 index, uint8 rsv`, 128 nibble-bytes (256 samples, self-contained) |
| `0x02` | end → transcribe, type, press Enter |
| `0x04` | end → discard (cancel) |

**Text characteristic `…0003` (write, Mac → device):**

| First byte | Meaning |
| --- | --- |
| `0x20` / `0x21` / `0x22` | text start-chunk / continuation / display |
| `0x30` + state | status: 0 ready · 1 running · 2 question · 3 stopped · 4 thinking · 5 working |

### Mac bridge (`tools/claude_mic.py`)

Scans → connects (shares the existing HID link) → decodes ADPCM → whisper.cpp
(`ggml-base.en`) → `osascript` keystrokes + Enter. Simultaneously tails
`~/.claude/projects/<project>/*.jsonl` and mirrors Claude's state and reply
text back to the device (thinking/tool_use/text blocks → status bytes;
`[Request interrupted` → STOPPED).

`tools/install_service.sh` wraps it in `~/Applications/ClaudeRemote.app`
(Info.plist carries the Bluetooth/Automation usage declarations) and installs
a KeepAlive LaunchAgent.

## Troubleshooting — the hard-won list

Every entry here cost real debugging time. Read before fighting the board.

- **Black screen but the device works** → the SPD2010 (a touch+display TDDI
  chip) latches up on power transitions (battery hot-plug especially). Only a
  *full* power removal recovers it: USB **and** battery out for 10s. On
  battery, holding PWR 3s = a true cold cycle.
- **Black screen and nothing works (app never boots)** → the download-mode
  trap: holding BOOT at plug-in sets a *persistent* force-download flag. Fix:
  true power-on reset, or `esptool write_mem 0x6000812C 0`. Avoid serial
  tools that toggle DTR/RTS; they can re-trigger it. Always end a flash
  session with a physical replug.
- **Device invisible to BLE scans** → it's connected; connected devices stop
  advertising. This firmware re-advertises while connected for exactly this
  reason.
- **`BleakCharacteristicNotFoundError`** → macOS cached the GATT table from an
  older firmware. Bluetooth Settings → Forget the device → re-pair. Required
  after any change to the service table.
- **Bridge crashes with a TCC/NSBluetoothAlwaysUsageDescription message** →
  the hosting process lacks Bluetooth privacy declarations. Use the installed
  service, or run from Terminal.app.
- **Whisper outputs `[Music]` / sound-effect captions** → transport works but
  the audio is noise. On this board the mic is on the **right** I2S slot;
  gain is the `>> 14` in `mic_stream.cpp` (try `>> 12` for 4×).
- **GATT service silently missing** → Bluedroid rejects app ids > `0x7FFF`
  without an event. Keep custom GATT app ids small.
- **Voice text landed in the wrong window** → keystrokes go to the focused
  app. Focus Claude's input before SEND.

## Project structure

```text
main/                 ESP-IDF firmware (C++, LVGL 9, Bluedroid)
tools/claude_mic.py   Mac bridge: BLE ⇄ whisper ⇄ keystrokes ⇄ transcript watcher
tools/install_service.sh  launchd service install/remove
tools/models/         whisper model (gitignored, ~141MB download)
tools/venv/           python env with bleak (gitignored)
```

## Ideas / not yet built

- QUESTION state wired to Claude's multiple-choice prompts; drag to choose,
  tap to answer
- Scrollable long replies on the device
- Bigger whisper model toggle for accuracy; mic gain auto-calibration
- Battery level in the status bar (ADC is available on the board)

---

Built at/after Granola Hardware Hack Day, pair-debugged with Claude — which
means Claude helped build the remote that controls Claude. 🎛️
