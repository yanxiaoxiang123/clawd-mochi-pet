<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi Pet 🦀🤖💫 (Smart Companion Edition)

**A lively smart desk pet** inspired by **Clawd** — the pixel-crab mascot of Claude Code by Anthropic. Powered by an **ESP32-C3 Super Mini** and a **1.54" 240×240 color TFT display** with **zero extra sensors or buttons**, it remembers your time together, senses your Apple Watch / iPhone proximity for contactless "Tap-to-Pet", feels hot & cold via its silicon thermometer, breathes through natural nap-and-wake cycles, and links over USB with your Mac & AI coding sessions.

> 🔀 **Forked from [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)** — keeps the exact same $7 hardware, wiring, and 3D-printed shell while upgrading the firmware into an autonomous + Mac-linked smart companion.

---

> ⚠️ This is an independent fan project. It is not affiliated with, sponsored by, or endorsed by Anthropic. "Claude" and "Clawd" are trademarks of Anthropic.

---

## ✨ What's New (Zero Extra Hardware Upgrade)

1. **🎭 15 Pure-Geometric Expressions (Zero Bitmap Memory)**
   - Rendered mathematically in real time via `Adafruit_GFX` while keeping a **pure, clean big-eye face** (no cluttered status bars):
   - `0` Normal · `1` Squish (`> <`) · `2` Heart (`♥ ♥`) · `3` Sparkle (`★ ★`) · `4` Wink · `5` Sleepy (`— —` + `Z`) · `6` Surprise (`⊙ ⊙`) · `7` Happy Arc (`^ ^`) · `8` Thinking (`• • •` AI scan) · `9` Angry/Tsundere (`\ /` + `╬`) · `10` Dizzy (`X X` + sweat) · `11` Cry (falling blue teardrops) · `12` Curious (`o O` + `?`) · `13` Yawn/Stretch · `14` Focus (visor glint).

2. **🌙 Natural Nap-and-Wake Cycle + Smooth PWM Backlight**
   - No longer gets permanently stuck in `SLEEPY` after 15 minutes!
   - Cycles naturally: **HYPER (45s) → HAPPY (4m) → CALM (10m) → Power Nap (3m)** with gentle breathing backlight on GPIO 3, then **stretches, yawns, and wakes back up on its own**.
   - Automatically dims to soft eye-care brightness late at night (`23:00–07:00`) or when you step away from your desk.

3. **🔐 Cryptographic iPhone BLE Bonding (IRK) & Contactless "Tap-to-Pet"**
   - Pair once in iPhone **Settings → Bluetooth → `Clawd-Mochi`** (uses HID Consumer Control so iOS virtual keyboard is never hidden, saves your 16-byte Identity Resolving Key in NVS, and immediately disconnects so zero phone battery is used).
   - **True 1-on-1 Owner Recognition**: Resolves your iPhone's rotating random BLE addresses via hardware AES-128 (`ah(IRK, prand)`):
     - **Tap-to-Pet**: Bring your iPhone close (~5–10 cm) to wake the crab up or trigger shy heart/smile eyes (`Affection +1`); if a stranger's phone touches the shell, it plays an angry tsundere face (`"哼! 你不是我主人!"`).
     - **Walk-Away & Welcome Back**: Automatically naps 90s after you walk out of the room with your iPhone (even if your Mac stays unlocked) and greets `"你回来啦!"` within seconds when you return.

4. **🧠 NVS Growth Memory, Full-GB2312 Adaptive Bubbles & Mini-Theater**
   - Remembers **boot count**, **total companion hours**, **affection level (`Lv.X`)**, and **owner iPhone key** across power cycles in ESP32 NVS Flash.
   - Full 7,000+ character **WenQuanYi GB2312** Chinese font (`16px` → `13px` → automatic 2-line `12px` wrap) so Chinese quips never drop characters or clip at the edges.
   - Reads the ESP32-C3 silicon temperature sensor (`temperatureRead()`) and plays occasional 3-line **Mini-Theater** monologues.

5. **💻 Real-Time Two-Way Web Console & Universal AI Coding Hooks**
   - **Live Web Console (`http://127.0.0.1:18765/preview.html`)**: Mirrors the physical crab's exact live expression, bubble text, backlight %, temperature, BLE RSSI, and Pomodoro bar every second, while letting you trigger any expression, scenario, or custom Chinese message on the real hardware with one click.
   - **Zero-Blocking AI Hooks (`tools/mochi_hook.py`)**: Automatically reacts live to **Antigravity**, **Claude Code**, and **Codex** (both Desktop GUI and CLI) — scanning while AI thinks/edits, curious `?` when waiting for your approval, and flashing celebration on completion.

---

## 💻 Mac Companion & AI Coding CLI (`tools/mochi.py`)

When plugged into your Mac via USB-C:

```bash
# Start background companion daemon (syncs clock, sitting health, CPU load, CLI & live web console at http://127.0.0.1:18765/preview.html)
python3 tools/mochi.py daemon

# Quick interactive commands (zero serial conflict even while daemon runs!)
python3 tools/mochi.py status                 # Inspect mood, Lv, companion time, temp & BLE RSSI
python3 tools/mochi.py pet                    # Pet the crab (+1 affection)
python3 tools/mochi.py say "先喝口水再写~"      # Show any Chinese/English speech bubble
python3 tools/mochi.py focus 25               # Start 25-min Pomodoro (3px bar at bottom edge)
python3 tools/mochi.py focus stop             # Stop Pomodoro bar
python3 tools/mochi.py theater                # Trigger a 3-line Mini-Theater monologue
python3 tools/mochi.py expr 8                 # Preview any expression (0..14)
python3 tools/mochi.py pair-ble               # Open 10-min iPhone Settings -> Bluetooth pairing ("Clawd-Mochi")
python3 tools/mochi.py unpair-ble             # Clear bonded iPhone identity key

# Manual / Script AI Hooks (or auto-wire Antigravity, Claude Code & Codex via tools/mochi_hook.py)
python3 tools/mochi.py ai think "正在重构模块..."
python3 tools/mochi.py ai ask   "等你拍板方案!"
python3 tools/mochi.py ai done  "编译通过啦!"
python3 tools/mochi.py git-commit
```

---

## 🔧 Hardware & Wiring (Unchanged)

> ⚠️ Connect VCC to **3.3V only** — never 5V. Use GPIO 8 and 10 for hardware SPI.

| Display pin | ESP32-C3 GPIO  | Wire color (suggested) |
| ----------- | -------------- | ---------------------- |
| VCC         | 3V3            | Red                    |
| GND         | GND            | Black                  |
| SDA         | GPIO 10 (MOSI) | Orange                 |
| SCL         | GPIO 8 (SCK)   | Green                  |
| RES         | GPIO 2         | Purple                 |
| DC          | GPIO 1         | Blue                   |
| CS          | GPIO 4         | White                  |
| BL          | GPIO 3 (PWM)   | Yellow                 |

---

## 🚀 Building & Flashing

### Via `arduino-cli` (Recommended)

Note: Select `PartitionScheme=huge_app` because the BLE stack + U8g2 Chinese font together exceed the 1.3 MB default partition.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,UploadSpeed=921600,PartitionScheme=huge_app clawd_mochi_nowifi
arduino-cli upload -p /dev/cu.usbmodem1201 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,UploadSpeed=921600,PartitionScheme=huge_app clawd_mochi_nowifi
```

### Via Arduino IDE 2.x

1. Install **esp32 by Espressif Systems (4.x recommended for BLE IRK bonding)** in Boards Manager.
2. Install libraries: `Adafruit GFX Library`, `Adafruit ST7735 and ST7789 Library`, `U8g2`, `U8g2_for_Adafruit_GFX`.
3. Under **Tools**, set:
   - **Board**: `ESP32C3 Dev Module`
   - **USB CDC On Boot**: `Enabled`
   - **Partition Scheme**: `Huge APP (3MB No OTA/1MB SPIFFS)`
   - **CPU Frequency**: `160MHz (WiFi)`
4. Open `clawd_mochi_nowifi/clawd_mochi_nowifi.ino` and click **Upload**.

---

## 📁 Project Structure

```
clawd-mochi-pet/
├── clawd_mochi_nowifi/           ← 📌 Firmware sketch folder
│   ├── clawd_mochi_nowifi.ino    ← 15 geometric expressions, PWM backlight & main loop
│   ├── mochi_types.h             ← Shared enums & event definitions
│   ├── dynamics.h                ← Natural nap/wake cycle, idle pool, reactions & easter eggs
│   ├── dynamics-speech.h         ← Mood/time/stat quips & 3-line Mini-Theater
│   ├── dynamics-sensors.h        ← NVS memory, silicon temperature & BLE Tap-to-Pet radar
│   └── dynamics-link.h           ← Non-blocking USB Serial protocol & 3px Pomodoro bar
├── tools/
│   ├── mochi.py                  ← 💻 Mac companion daemon, CLI & live HTTP server (:18765)
│   ├── mochi_hook.py             ← 🤖 Zero-blocking hook bridge for Antigravity, Claude Code & Codex
│   └── check_glyphs.py           ← 🛡️ GB2312 full-charset & bubble guard check
├── preview.html                  ← 🎨 Real-time two-way hardware console & offline animation showcase
├── models/                       ← 3D-printable shell STL files
├── pics/                         ← Reference photos
└── README.md
```

---

## License

MIT License — see [LICENSE](LICENSE). 3D models and media assets are licensed under **CC BY-NC-SA 4.0** (inherited from [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)).
