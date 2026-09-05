#!/usr/bin/env python3
"""
Claude Remote voice bridge.

Receives IMA-ADPCM audio streamed from the device's onboard mic over BLE,
transcribes it locally with whisper.cpp, then types the text into the
frontmost app and presses Enter (grant Accessibility permission to your
terminal the first time).

Usage:  ./venv/bin/python claude_mic.py
"""
import asyncio
import json
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "Claude Remote"
AUDIO_CHAR = "7a0d0002-c1a0-de50-b3a4-4b1d4e101001"
TEXT_CHAR = "7a0d0003-c1a0-de50-b3a4-4b1d4e101001"
TRANSCRIPT_DIR = Path.home() / ".claude/projects/-Users-rxshri99-Projects-hackathons-granoala"
SAMPLE_RATE = 16000
WHISPER = "whisper-cli"
MODEL = Path(__file__).parent / "models" / "ggml-base.en.bin"

AUD_START, AUD_END_SEND, AUD_FRAME, AUD_END_CANCEL = 1, 2, 3, 4

STEP_TAB = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767]
INDEX_TAB = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_decode_block(block: bytes) -> bytes:
    """block: int16 predictor, uint8 index, uint8 rsv, nibbles."""
    predictor, index = struct.unpack_from("<hB", block, 0)
    out = bytearray()
    for byte in block[4:]:
        for code in (byte & 0x0F, byte >> 4):
            step = STEP_TAB[index]
            delta = step >> 3
            if code & 4:
                delta += step
            if code & 2:
                delta += step >> 1
            if code & 1:
                delta += step >> 2
            predictor += -delta if (code & 8) else delta
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + INDEX_TAB[code & 0x0F]))
            out += struct.pack("<h", predictor)
    return bytes(out)


def transcribe(pcm: bytes) -> str:
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        wav_path = f.name
    with wave.open(wav_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm)
    r = subprocess.run(
        [WHISPER, "-m", str(MODEL), "-f", wav_path, "-nt",
         "--no-prints", "-l", "en"],
        capture_output=True, text=True, timeout=120)
    return r.stdout.strip()


def type_and_send(text: str):
    esc = text.replace("\\", "\\\\").replace('"', '\\"')
    script = f'''
tell application "System Events"
    keystroke "{esc}"
    delay 0.15
    key code 36
end tell'''
    subprocess.run(["osascript", "-e", script], capture_output=True)


async def send_text(client, text: str):
    """0x20 start / 0x21 append / 0x22 show, 150-byte chunks."""
    data = text.encode()[:560]
    op = 0x20
    for i in range(0, max(len(data), 1), 150):
        await client.write_gatt_char(TEXT_CHAR, bytes([op]) + data[i:i + 150], response=False)
        op = 0x21
    await client.write_gatt_char(TEXT_CHAR, b"\x22", response=False)


async def send_status(client, state: int):
    """0=ready 1=running 2=question 3=stopped"""
    await client.write_gatt_char(TEXT_CHAR, bytes([0x30, state]), response=False)


def extract_text(msg) -> str:
    content = msg.get("content")
    if isinstance(content, str):
        return content
    parts = []
    for c in content or []:
        if isinstance(c, dict) and c.get("type") == "text":
            parts.append(c.get("text", ""))
    return "\n".join(parts).strip()


async def watch_claude(client):
    """Tail the newest Claude transcript; mirror replies + status to the device."""
    pos, current = 0, None
    while client.is_connected:
        await asyncio.sleep(1)
        files = sorted(TRANSCRIPT_DIR.glob("*.jsonl"), key=lambda f: f.stat().st_mtime)
        if not files:
            continue
        newest = files[-1]
        if newest != current:
            current, pos = newest, newest.stat().st_size  # start at end
            continue
        size = newest.stat().st_size
        if size <= pos:
            continue
        with open(newest) as f:
            f.seek(pos)
            chunk = f.read()
            pos = f.tell()
        for line in chunk.splitlines():
            try:
                d = json.loads(line)
            except Exception:
                continue
            if d.get("isSidechain"):
                continue
            try:
                if d.get("type") == "user" and not d.get("isMeta"):
                    raw = extract_text(d.get("message", {})) or ""
                    if "[Request interrupted" in raw:
                        await send_status(client, 3)  # stopped
                        await send_text(client, "(stopped)")
                        print("⇠ stopped", flush=True)
                    else:
                        await send_status(client, 1)  # running (msg or tool result)
                elif d.get("type") == "assistant":
                    blocks = d.get("message", {}).get("content") or []
                    kinds = [b.get("type") for b in blocks if isinstance(b, dict)]
                    if "thinking" in kinds:
                        await send_status(client, 4)
                        print("⇠ thinking", flush=True)
                    if "tool_use" in kinds:
                        await send_status(client, 5)
                        print("⇠ working", flush=True)
                    text = extract_text(d.get("message", {}))
                    if text:
                        await send_status(client, 0)  # ready
                        await send_text(client, "Claude: " + text)
                        print(f"⇠ mirrored {len(text)} chars", flush=True)
            except Exception as e:
                print("mirror failed:", e)


class Session:
    def __init__(self, client=None):
        self.client = client
        self.pcm = bytearray()
        self.active = False

    def on_notify(self, _, data: bytearray):
        kind = data[0] if data else 0
        if kind == AUD_START:
            self.pcm.clear()
            self.active = True
            print("● recording...", flush=True)
        elif kind == AUD_FRAME and self.active:
            self.pcm += adpcm_decode_block(bytes(data[1:]))
        elif kind == AUD_END_CANCEL:
            self.active = False
            self.pcm.clear()
            print("✗ cancelled", flush=True)
        elif kind == AUD_END_SEND:
            self.active = False
            secs = len(self.pcm) / 2 / SAMPLE_RATE
            print(f"■ {secs:.1f}s captured, transcribing...", flush=True)
            if secs < 0.4:
                print("  (too short, ignored)")
                return
            text = transcribe(bytes(self.pcm))
            self.pcm.clear()
            if text:
                print(f"→ {text!r}")
                type_and_send(text)
                if self.client:
                    asyncio.get_event_loop().create_task(
                        send_text(self.client, "You: " + text))
            else:
                print("  (nothing recognized)")
                if self.client:
                    asyncio.get_event_loop().create_task(
                        send_text(self.client, "(nothing recognized)"))


async def main():
    if not MODEL.exists():
        sys.exit(f"model missing: {MODEL}")
    print(f"looking for '{DEVICE_NAME}'...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15)
    if dev is None:
        sys.exit("device not found — is it on and paired?")
    session = Session()
    async with BleakClient(dev) as client:
        session.client = client
        await client.start_notify(AUDIO_CHAR, session.on_notify)
        print("connected — tap MIC on the device and speak into it")
        await send_text(client, "voice bridge online")
        await watch_claude(client)
    print("device disconnected")


if __name__ == "__main__":
    asyncio.run(main())
