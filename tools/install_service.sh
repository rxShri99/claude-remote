#!/usr/bin/env bash
# Install (or remove) the Claude Remote voice bridge as a login background
# service. Builds a minimal app bundle so macOS privacy prompts (Bluetooth,
# Accessibility/Automation) attribute correctly — a bare python under launchd
# gets TCC-killed instead of prompted.
#
# Usage:  ./install_service.sh          install + start
#         ./install_service.sh remove   stop + uninstall
set -euo pipefail

TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$HOME/Applications/ClaudeRemote.app"
AGENT_ID="com.granoala.claude-remote"
AGENT_PLIST="$HOME/Library/LaunchAgents/$AGENT_ID.plist"
LOG="$HOME/Library/Logs/claude-remote.log"

if [ "${1:-}" = "remove" ]; then
    launchctl bootout "gui/$(id -u)/$AGENT_ID" 2>/dev/null || true
    rm -f "$AGENT_PLIST"
    rm -rf "$APP"
    echo "service removed"
    exit 0
fi

# ---- app bundle wrapper -----------------------------------------------------
mkdir -p "$APP/Contents/MacOS"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key><string>$AGENT_ID</string>
    <key>CFBundleName</key><string>ClaudeRemote</string>
    <key>CFBundleExecutable</key><string>claude-remote</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>LSUIElement</key><true/>
    <key>NSBluetoothAlwaysUsageDescription</key>
    <string>Receives voice audio from the Claude Remote device.</string>
    <key>NSAppleEventsUsageDescription</key>
    <string>Types transcribed voice commands into the focused app.</string>
</dict>
</plist>
PLIST

cat > "$APP/Contents/MacOS/claude-remote" <<LAUNCH
#!/bin/bash
exec "$TOOLS_DIR/venv/bin/python" -u "$TOOLS_DIR/claude_mic.py"
LAUNCH
chmod +x "$APP/Contents/MacOS/claude-remote"
codesign --force --deep --sign - "$APP" 2>/dev/null || true

# ---- launch agent -----------------------------------------------------------
mkdir -p "$(dirname "$AGENT_PLIST")" "$(dirname "$LOG")"
cat > "$AGENT_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$AGENT_ID</string>
    <key>ProgramArguments</key>
    <array><string>$APP/Contents/MacOS/claude-remote</string></array>
    <key>RunAtLoad</key><true/>
    <key>KeepAlive</key><true/>
    <key>ThrottleInterval</key><integer>10</integer>
    <key>StandardOutPath</key><string>$LOG</string>
    <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PLIST

launchctl bootout "gui/$(id -u)/$AGENT_ID" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$AGENT_PLIST"
launchctl kickstart -k "gui/$(id -u)/$AGENT_ID"

echo "installed and started."
echo "  logs:      tail -f $LOG"
echo "  restart:   launchctl kickstart -k gui/$(id -u)/$AGENT_ID"
echo "  remove:    $0 remove"
echo
echo "First run: approve the Bluetooth prompt for ClaudeRemote, and grant"
echo "Accessibility when the first voice message is typed."
