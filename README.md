<!-- LOGO -->
<p align="center">
  <img src="pics/clawd_mochi_banner.png" alt="Clawd Mochi Logo" width="700"/>
</p>

# Clawd Mochi Pet 🦀🤖💫

**A lively desk pet** inspired by **Clawd** — the pixel-crab mascot of Claude Code by Anthropic. An ESP32-C3 drives a 1.54" color TFT display and runs a self-contained personality system that drifts through moods, blinks, glances, reacts, and surprises — no WiFi, no app, no cloud, no buttons.

> 🔀 **This is a fork of [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi)** that replaces the WiFi web controller with a **lively pet dynamic system** that runs entirely on the device.

**Cost: ~$6–8 · Build time: ~1 hour · Skill level: Beginner**

📦 3D printable case on MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

---

> ⚠️ This is an independent fan project. It is not affiliated with, sponsored by, or endorsed by Anthropic. "Claude" and "Clawd" are trademarks of Anthropic.

---

## What it does

Clawd Mochi sits on your desk and **lives** on a small color display. Plug it in, and the pet wakes up **hyper**, then over time drifts through moods — **happy → calm → sleepy** — making it feel like a small creature that responds to the world around it.

### 6 main expressions

| # | Expression | What it looks like |
|---|------------|-------------------|
| 0 | Normal eyes | Pixel-art square eyes with wiggle and blink |
| 1 | Squish eyes | `> <` happy squint, opens and closes |
| 2 | Heart eyes | Pulsing hearts that grow and shrink |
| 3 | Sparkle eyes | Star-shaped eyes with rotation |
| 4 | Wink | One eye closes, then opens with sparkle |
| 5 | Sleepy | Half-closed eyes with a floating Z |

### 5 layered behavior systems (the pet part)

These run automatically on top of the main expressions:

| System | What it does |
|--------|--------------|
| **🧠 Mood** | Time-since-boot: HYPER (0–30s) → HAPPY (30s–3min) → CALM (3–15min) → SLEEPY (15min+). Mood changes are visible — animation speed and idle frequency shift. |
| **👀 Idle micro-animations** | Random blinks, glances (L/R/up), breath pulses, and shivers between main expressions. The pool is **weighted by mood** — HYPER pet glances around a lot; SLEEPY pet just shivers. |
| **⚡ Reaction chains** | After a main expression, there's a 40–80% chance of a follow-up: Wink → often Sparkle; Sparkle → often an extra glance; Sleepy → often one more Z floats up. |
| **🥚 Easter eggs** | 4 rare surprise animations fire at mood-dependent rates: **ZOOM** (eyes pulse huge then tiny), **DANCE** (all 6 expressions rapid-fire), **FLASH** (background strobes), **SECRET** (a 7th hidden shocked face `⊙⊙`). |
| **💬 Random speech** | Mood-aware Chinese + English quips appear in a transparent bubble at the bottom of the screen. **吃了吗?**, **想你呀**, **zzz...** — text drifts with mood, with 80% probability tied to current mood and 20% to a boot-time randomized "voice". |

---

## What's different from the original

| | Original (`yousifamanuel/clawd-mochi`) | This fork |
|---|---|---|
| Control | WiFi hotspot + phone browser | **No WiFi** — fully autonomous |
| Expressions | 4 (Normal, Squish, Claude Code, Canvas) | **6 + 1 hidden** (Normal, Squish, Heart, Sparkle, Wink, Sleepy, +Surprise in easter egg) |
| Behavior | Static user-triggered | **4 layered systems** (mood, idle, reactions, easter eggs) |
| Sketch size | 1087 lines (single .ino) | 428 lines (.ino) + 373 lines (dynamics.h) + 200 lines (dynamics-speech.h) |
| Web controller | Yes | **Removed** |
| Hardware pinout | Identical | Identical |

**Same hardware, same wiring, same 3D case — just a different sketch.**

---

## Parts list

| Part                | Spec                             | ~Price |
| ------------------- | -------------------------------- | ------ |
| ESP32-C3 Super Mini | microcontroller                  | ~$2.50 |
| ST7789 1.54" TFT    | 240×240 SPI color display        | ~$3.00 |
| 8 short wires       | 8–10 cm Dupont / jumper wires    | ~$0.50 |
| 2× M2×6mm screws    | to mount display bezel           | ~$0.10 |
| Double-sided tape   | to secure components inside case | ~$0.10 |
| USB-C cable         | for power                        | —      |
| 3D printed case     | PLA or PETG, ~30g                | ~$0.50 |

**Total: ~$7–8**

---

## Wiring

> ⚠️ Connect VCC to **3.3V only** — never 5V. Use GPIO 8 and 10 for SPI (hardware SPI, fast). Do not use GPIO 6/7 for SPI.

| Display pin | ESP32-C3 GPIO  | Wire color (suggested) |
| ----------- | -------------- | ---------------------- |
| VCC         | 3V3            | Red                    |
| GND         | GND            | Black                  |
| SDA         | GPIO 10 (MOSI) | Orange                 |
| SCL         | GPIO 8 (SCK)   | Green                  |
| RES         | GPIO 2         | Purple                 |
| DC          | GPIO 1         | Blue                   |
| CS          | GPIO 4         | White                  |
| BL          | GPIO 3         | Yellow                 |

---

## Software setup

### Step 1 — Install Arduino IDE

Download [Arduino IDE 2.x](https://www.arduino.cc/en/software) and install it.

### Step 2 — Add ESP32 board support

1. Open Arduino IDE → **File → Preferences**
2. In "Additional boards manager URLs" paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **"esp32 by Espressif Systems"** (3.3.x)

### Step 3 — Install libraries

Go to **Tools → Library Manager** and install all four:

- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `U8g2` (by olikraus) — for Chinese font rendering in speech bubbles
- `U8g2_for_Adafruit_GFX` (by olikraus) — bridge between U8g2 fonts and Adafruit GFX

> 💡 The U8g2 + U8g2_for_Adafruit_GFX libraries are only needed for the **random speech bubble** feature (Chinese text rendering). The 4 behavior systems (mood / idle / reactions / easter eggs) work without them.

### Step 4 — Configure board settings

Go to **Tools** and set:

| Setting         | Value                   |
| --------------- | ----------------------- |
| Board           | ESP32C3 Dev Module      |
| USB CDC On Boot | **Enabled** ← important |
| CPU Frequency   | 160 MHz                 |
| Upload Speed    | 921600                  |

### Step 5 — Upload the sketch

1. Clone or download this repo
2. Open **`clawd_mochi_nowifi/clawd_mochi_nowifi.ino`** in Arduino IDE (the .h file is in the same folder and will be auto-included)
3. Connect the ESP32 via USB-C
4. Select the correct port under **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Hard resetting via RTS pin..." — this means success
7. **Unplug and replug the USB** — this restarts the ESP32 so the pet wakes up fresh

---

## How to use it

### Just plug it in

That's it. No phone, no WiFi, no buttons. The pet runs itself.

1. Power the ESP32 via USB-C (any USB charger or power bank)
2. Wait ~2 seconds for the first expression to appear
3. Watch it live

### What you'll see

| Time since boot | Mood | Behavior |
|-----------------|------|----------|
| 0 – 30 s | 🟠 HYPER | Fast expressions, lots of easter eggs, eyes darting around, frequent speech |
| 30 s – 3 min | 🟡 HAPPY | Standard pace, normal idle mix, occasional reactions, regular speech |
| 3 – 15 min | 🟢 CALM | Slower, more breath and blink idles, rarer easter eggs, gentler speech |
| 15 min+ | 🔵 SLEEPY | Very slow, shiver replaces glances, no easter eggs, sleepy quips only |

**Tip:** if you want to "reset" the pet to HYPER, just unplug and replug the USB.

### Speech bubbles 💬

Every few seconds (depending on mood — roughly every 15-30s in HAPPY), the pet will say something in a **transparent** bubble at the bottom of the screen — just an orange rounded-rectangle outline with white text floating on the background. Quips mix Chinese and English, drift with mood:

- 🟠 HYPER: `Let's gooo!` `能量满满` `冲冲冲~`
- 🟡 HAPPY: `吃了吗?` `想你呀` `摸鱼快乐~`
- 🟢 CALM: `慢慢来~` `喝口水` `深呼吸...`
- 🔵 SLEEPY: `zzz...` `好困呀` `(揉眼睛)`

80% of the time the quip is from the current mood; 20% it's from a boot-time randomized "voice" so the pet feels different each power cycle. Add or edit quips in `dynamics-speech.h` (`QUIPS_HYPER[]`, `QUIPS_HAPPY[]`, `QUIPS_CALM[]`, `QUIPS_SLEEPY[]`).

> 📝 The bubble is **non-blocking** and **transparent** — eye animations continue uninterrupted behind it. Chinese text is rendered via the U8g2 `unifont_t_chinese1` font (3,755 GB2312 level-1 characters). The `.ino` file uses a custom inline UTF-8 decoder to render multi-byte Chinese characters since the `U8G2_FOR_ADAFRUIT_GFX` bridge only exposes `drawGlyph` (single character).

---

## 3D case

The electronics case is **identical** to the original project — see [`models/clawd_mochi/`](./models/clawd_mochi/) for the body and back plate STL files. Print settings:

| Setting      | Value                               |
| ------------ | ----------------------------------- |
| Material     | PLA or PETG                         |
| Layer height | 0.15–0.20 mm                        |
| Infill       | 15% gyroid                          |
| Supports     | Yes — for display window overhang   |
| Orientation  | Face-down, flat back on build plate |

Suggested colors: orange PLA for body, matte black for back plate.

You can also download the models from MakerWorld: [https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000](https://makerworld.com/en/models/2559505-clawd-mochi-physical-claude-code-mascot#profileId-2820000)

---

## Project structure

```
clawd-mochi-pet/
├── clawd_mochi_nowifi/           ← 📌 upload this folder
│   ├── clawd_mochi_nowifi.ino    ← main sketch (6 expressions + loop scheduler)
│   ├── dynamics.h                ← mood + idle + reactions + easter eggs
│   └── dynamics-speech.h         ← 💬 random speech bubbles (Chinese + English)
├── models/                       ← 3D-printable cases (from original)
├── pics/                         ← reference photos (from original)
├── docs/
│   └── superpowers/
│       ├── specs/                ← design docs
│       │   └── 2026-06-29-clawd-mochi-dynamics-design.md
│       └── plans/                ← implementation plans
│           └── 2026-06-29-clawd-mochi-dynamics.md
├── README.md
└── LICENSE
```

---

## Customisation

All tunables are in `clawd_mochi_nowifi/dynamics.h` and `clawd_mochi_nowifi/dynamics-speech.h` (top of each file):

| Want to change | File / location |
|----------------|------|
| Mood transition timing | `dynamics.h` — `MOOD_HYPER_END_MS` / `MOOD_HAPPY_END_MS` / `MOOD_CALM_END_MS` |
| Easter egg frequency | `dynamics.h` — `EGGS[]` probability array per mood |
| Idle frequency | `.ino` loop() — `if (random(100) < 60)` |
| Reaction probability | `dynamics.h` — `REACTIONS[]` probability field |
| Idle pool bias | `dynamics.h` — `IDLE_WEIGHTS_*[]` arrays |
| 7th secret face | `.ino` — `drawSurpriseEyes()` body |
| Speech frequency | `dynamics-speech.h` — `SPEAK_PROB[4]` array (per mood) |
| Speech minimum gap | `dynamics-speech.h` — `SPEAK_COOLDOWN_MS` |
| Quip text / pool | `dynamics-speech.h` — `QUIPS_HYPER[]` / `QUIPS_HAPPY[]` / `QUIPS_CALM[]` / `QUIPS_SLEEPY[]` |
| Speech bubble position | `dynamics-speech.h` — `boxY` constant |
| Speech text color | `dynamics-speech.h` — `setForegroundColor(C_WHITE)` |
| Speech border color | `dynamics-speech.h` — `tft.drawRoundRect(...)` color arg |

Want to disable easter eggs entirely? Set all of `EGGS[i].probability[mood]` to 0.
Want to disable speech entirely? Comment out step 6 in `loop()` (the `if (shouldSpeak())` block).

---

## How the dynamic system works

```
loop() does this every iteration:
  0. clearQuipArea()    // remove expired speech bubble
  1. updateMood()        // check if HYPER→HAPPY→CALM→SLEEPY transition
  2. main expression     // one of 6 animXxxEyes() (~4s)
  3. reactAfter()        // 40-80% chance, append reaction (~1-2s)
  4. playRandomIdle()    // 60% chance, append micro-animation (~0.5-1s)
  5. playEasterEgg()     // mood-gated, append surprise (~1.5-2.5s)
  6. shouldSpeak()       // mood-gated, draw a Chinese/English quip bubble
  7. advance step
```

Total loop length: 4-9 seconds depending on mood. See [the design spec](docs/superpowers/specs/2026-06-29-clawd-mochi-dynamics-design.md) for the full data structures and probabilities.

> 💡 Eye animations only clear the **y < 195** region (`tft.fillRect(0, 0, 240, 195, bg)`), so the speech bubble at y=200-228 is never wiped by the eyes. The bubble's lifetime is governed by `quipDrawnMs` + `quipDurationMs` (typically 1.5-3.5s), independent of loop cadence.

---

## Contributing

Contributions welcome! Ideas:

- **More expressions** — add new `animXxxEyes()` functions
- **More idle behaviors** — yawn, stretch, look-around patterns
- **More easter eggs** — add a new function and a new entry in `EGGS[]`
- **Physical interaction** — add a button to pet/tickle the crab
- **Persistence** — remember mood across reboots with NVS
- **Battery + deep sleep** — make it truly portable

To contribute: fork this repo, make your changes, open a pull request.

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

**Note:** 3D models and media assets are licensed under **CC BY-NC-SA 4.0** (inherited from the original project).

---

## Credits

- Original project: [yousifamanuel/clawd-mochi](https://github.com/yousifamanuel/clawd-mochi) — hardware design, base sketch, 3D models
- Lively pet dynamics: added in this fork (2026-06-29)
- Design + plan docs in `docs/superpowers/`
