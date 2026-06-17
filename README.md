# 🎮 Retro-Gamer

**Multi-System Retro Emulator for ESP32**

A feature-rich retro gaming console built on the **Olimex ESP32-SBC-FabGL Rev B** board. Play NES, Game Boy, Sega Master System, PC Engine, SNES, Atari Lynx, Mega Drive, Game & Watch, and even DOOM — all through VGA output with PS/2 keyboard input.

---

## Table of Contents

- [Features](#features)
- [Supported Systems](#supported-systems)
- [Hardware Requirements](#hardware-requirements)
- [Pin Configuration](#pin-configuration)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Install Tools](#install-tools)
  - [Compile & Flash](#compile--flash)
- [SD Card Setup](#sd-card-setup)
- [Keyboard Controls](#keyboard-controls)
- [In-Game Menu](#in-game-menu)
- [WiFi File Manager](#wifi-file-manager)
- [Cover Art](#cover-art)
- [Debug System](#debug-system)
- [Build Configuration](#build-configuration)
- [Architecture](#architecture)
- [Retro-Go Compatibility](#retro-go-compatibility)
- [Troubleshooting](#troubleshooting)
- [Known Limitations](#known-limitations)
- [Credits & Licenses](#credits--licenses)

---

## Features

| Feature | Description |
|---------|-------------|
| **10 Emulators + DOOM** | NES, GB/GBC, SMS, GG, SG-1000, ColecoVision, PCE, SNES, Lynx, G&W, Genesis, DOOM |
| **In-Game Menu** | Save/Load states (4 slots), turbo, palettes, scaling, reset |
| **17 GB Palettes** | DMG Green, Pocket, MGB Light, CGB, SGB, and custom schemes |
| **5 NES Palettes** | 2C02, Smooth, Nesticle, Wavebeam, Classic |
| **Favorites & Recent** | Quick access to your favorite and recently played games |
| **Cover Art** | ROM artwork displayed in launcher (retro-go .art format) |
| **ZIP Support** | Load ROMs directly from ZIP files (Stored + Deflate) |
| **WiFi File Manager** | Browser-based file upload/download (compile-time optional) |
| **NTP Time Sync** | Automatic clock sync for GB RTC games |
| **GB SRAM Auto-Save** | Battery save auto-backup every ~5 seconds |
| **NSF Music Player** | Play NES Sound Format music files |
| **Turbo / Fast Forward** | 3× speed with frame skip |
| **Debug OSD** | Real-time FPS, memory stats overlay (F12 toggle) |
| **DOOM Extras** | Automap, screen wipes, cheats (IDDQD!), OPL FM music |

---

## Supported Systems

| System | Core | Extensions | Resolution | Status |
|--------|------|------------|------------|--------|
| NES / Famicom | nofrendo | `.nes` `.fc` `.fds` `.nsf` | 256×240 | ✅ Stable |
| Game Boy | gnuboy | `.gb` | 160×144 | ✅ Stable |
| Game Boy Color | gnuboy | `.gbc` | 160×144 | ✅ Stable |
| Sega Master System | smsplus | `.sms` | 256×192 | ✅ Stable |
| Sega Game Gear | smsplus | `.gg` | 160×144 | ✅ Stable |
| SG-1000 | smsplus | `.sg` `.sg1` | 256×192 | ✅ Stable |
| ColecoVision | smsplus | `.col` | 256×192 | ✅ Stable |
| PC Engine / TurboGrafx | huexpress | `.pce` | 256×224 | ✅ Stable |
| SNES / Super Famicom | snes9x | `.sfc` `.smc` | 256×224 | ⚠️ Slow |
| Atari Lynx | handy | `.lnx` | 160×102 | ✅ Stable |
| Game & Watch | custom | `.gw` | 320×240 | ✅ Stable |
| Mega Drive / Genesis | gwenesis | `.md` `.gen` `.bin` | 320×224 | ✅ Stable |
| DOOM | prboom | `.wad` | 320×200 | ✅ Stable |
| MSX | fMSX (stub) | `.rom` `.mx1` `.mx2` `.dsk` | 256×192 | 🔲 Needs BIOS |

> All systems support `.zip` files — ROMs are automatically extracted.

---

## Hardware Requirements

| Component | Specification |
|-----------|--------------|
| **Board** | [Olimex ESP32-SBC-FabGL Rev B](https://www.olimex.com/Products/IoT/ESP32/ESP32-SBC-FabGL/) |
| **MCU** | ESP32-WROVER (dual-core 240MHz, 4MB Flash, 4MB PSRAM) |
| **Display** | VGA monitor (640×480 minimum, 320×240 rendered) |
| **Input** | PS/2 Keyboard (directly connected, no adapter needed) |
| **Storage** | MicroSD card, FAT32 formatted, ≤32GB recommended |
| **Audio** | Connected to GPIO 25 (internal DAC), amplified speaker optional |
| **Power** | 5V via Micro-USB |

---

## Pin Configuration

The Olimex ESP32-SBC-FabGL Rev B has all pins pre-wired. For reference or custom builds:

| Function | GPIO | Notes |
|----------|------|-------|
| VGA Red 0 | 21 | LSB red |
| VGA Red 1 | 22 | MSB red |
| VGA Green 0 | 18 | LSB green |
| VGA Green 1 | 19 | MSB green |
| VGA Blue 0 | 4 | LSB blue |
| VGA Blue 1 | 5 | MSB blue |
| VGA HSync | 23 | Horizontal sync |
| VGA VSync | 15 | Vertical sync |
| SD Card CS | 13 | Chip select |
| SD Card MISO | 35 | Data out |
| SD Card MOSI | 12 | Data in |
| SD Card CLK | 14 | Clock |
| PS/2 Keyboard CLK | 33 | Clock line |
| PS/2 Keyboard DAT | 32 | Data line |
| PS/2 Mouse CLK | 26 | Clock (optional) |
| PS/2 Mouse DAT | 27 | Data (optional) |
| Audio DAC | 25 | Internal 8-bit DAC |

---

## Getting Started

### Prerequisites

- **Arduino CLI** (recommended) or Arduino IDE 2.x
- **ESP32 Arduino Core** version 2.0.11
- **FabGL** library version 1.0.9

### Install Tools

#### 1. Arduino CLI

```bash
# Windows (PowerShell)
winget install Arduino.ArduinoCLI

# macOS
brew install arduino-cli

# Linux
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

#### 2. ESP32 Board Support

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@2.0.11
```

#### 3. FabGL Library

```bash
arduino-cli lib install "FabGL@1.0.9"
```

### Compile & Flash

#### Compile

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32wrover:PartitionScheme=huge_app,FlashFreq=80" \
  /path/to/Retro-Gamer
```

Windows PowerShell:
```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32wrover:PartitionScheme=huge_app,FlashFreq=80" "C:\path\to\Retro-Gamer"
```

#### Flash / Upload

```bash
arduino-cli upload \
  --fqbn "esp32:esp32:esp32wrover:PartitionScheme=huge_app,FlashFreq=80" \
  -p /dev/ttyUSB0 \
  /path/to/Retro-Gamer
```

> **Windows**: Replace `/dev/ttyUSB0` with your COM port (e.g., `COM3`). Check Device Manager → Ports.

#### Serial Monitor (for debug output)

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

---

## SD Card Setup

Format a MicroSD card as **FAT32** (≤32GB). Create the following directory structure:

```
📁 SD Card Root
├── 📁 roms/
│   ├── 📁 nes/          ← .nes .fc .fds .nsf .zip
│   ├── 📁 gb/           ← .gb .gbc .zip
│   ├── 📁 sms/          ← .sms .zip
│   ├── 📁 gg/           ← .gg .zip
│   ├── 📁 pce/          ← .pce .zip
│   ├── 📁 snes/         ← .sfc .smc .zip
│   ├── 📁 lnx/          ← .lnx .zip
│   ├── 📁 gw/           ← .gw
│   ├── 📁 col/          ← .col .zip
│   ├── 📁 md/           ← .md .gen .bin .zip
│   ├── 📁 doom/         ← .wad .zip
│   └── 📁 msx/          ← .rom .mx1 .mx2 .dsk
├── 📁 romart/            ← Cover art (optional)
│   ├── 📁 nes/
│   ├── 📁 gb/
│   └── 📁 ...
└── 📁 retro-go/
    ├── 📁 config/
    │   ├── wifi.json     ← WiFi config (optional)
    │   ├── favorites.txt ← Auto-generated
    │   └── recent.txt    ← Auto-generated
    ├── 📁 saves/         ← Save states (auto-created)
    │   ├── 📁 nes/
    │   ├── 📁 gb/
    │   └── 📁 ...
    ├── 📁 bios/
    │   └── MSX.ROM       ← MSX BIOS (user-provided)
    └── 📁 temp/          ← ZIP extraction (auto-created)
```

> **Important**: ROM files must be placed in the correct platform subdirectory under `/roms/`. The launcher scans all subdirectories automatically.

---

## Keyboard Controls

### Launcher

| Key | Action |
|-----|--------|
| ↑ / ↓ | Navigate ROM list |
| Enter | Load and play selected ROM |
| F | Toggle favorite (⭐) |
| Tab | Cycle view: All → Favorites → Recent |
| S | Cycle scaling: 1:1 → Fit → Stretch |
| W | Toggle WiFi (if enabled) |
| Esc | Rescan ROMs |

### In-Game (Emulators)

| Key | Function |
|-----|----------|
| ↑ ↓ ← → | D-pad |
| W A S D | D-pad (alternative) |
| Z / Space | A button |
| X / Left Ctrl | B button |
| Enter | Start |
| Right Shift | Select |
| Tab | Toggle turbo (3× speed) |
| Esc | Open in-game menu |
| F12 | Toggle debug OSD overlay |

### In-Game (DOOM specific)

| Key | Function |
|-----|----------|
| ↑ ↓ | Move forward / backward |
| ← → | Turn left / right |
| Space / Z | Fire weapon |
| X / Ctrl | Use / Open door |
| Tab | Automap |
| 1-7 | Select weapon |

---

## In-Game Menu

Press **Esc** during gameplay to open the in-game menu:

```
┌─────────────────────┐
│   ▶ Resume          │
│   💾 Save State [0] │
│   📂 Load State [0] │
│   ⏩ Turbo: OFF     │
│   🎨 Palette: 0     │
│   📐 Scale: 1:1     │
│   🔄 Reset          │
│   🚪 Quit to Menu   │
└─────────────────────┘
```

- **↑/↓**: Navigate items
- **Enter**: Select item
- **←/→**: Change slot (Save/Load), palette, or scale mode
- **Save States**: 4 slots (0-3) per game

---

## WiFi File Manager

WiFi is disabled by default to save DRAM. To enable:

1. Open `Retro-Gamer.ino` and change `#define ENABLE_WIFI 0` to `1`
2. Rename `src/wifi_manager.cpp.disabled` → `src/wifi_manager.cpp`
3. Recompile and flash

### WiFi Configuration

Create `/retro-go/config/wifi.json` on your SD card:

```json
{
  "ssid0": "MyNetwork",
  "password0": "MyPassword",
  "ssid1": "AlternateNetwork",
  "password1": "AltPassword"
}
```

Up to 4 networks can be configured (ssid0-ssid3).

### Usage

1. Press **W** in the launcher to connect
2. IP address is displayed on screen
3. Open `http://<IP>` in any browser
4. Browse, upload, download, and delete files on the SD card

> **Note**: WiFi adds ~31KB DRAM overhead from the ESP32 SDK. This is a hardware limitation of the Arduino ESP32 platform.

---

## Cover Art

Display game cover art in the launcher when selecting ROMs.

1. Download cover art from [retro-go-covers](https://github.com/ducalex/retro-go-covers)
2. Place `.art` files in `/romart/{emu}/` on your SD card
3. File names should match ROM names (e.g., `SuperMario.art` for `SuperMario.nes`)

Cover art format: Raw RGB565 Little-Endian with width×height header, converted to VGA8 64-color on display.

---

## Debug System

A compile-time togglable debug system provides both Serial and VGA overlay output.

### Configuration (in `Retro-Gamer.ino`)

```cpp
#define DEBUG_LEVEL 3     // 0=OFF, 1=ERR, 2=WARN, 3=INFO, 4=VERBOSE
#define DEBUG_VGA_OSD 1   // 0=OFF, 1=ON (F12 toggle)
```

### Serial Output

Color-coded log messages at 115200 baud:

```
[ERR][Audio] PSRAM alloc failed!          ← Red
[WRN][Boot]  Low memory warning           ← Yellow
[INF][ROM]   Loading: SuperMario.nes      ← White
[VRB][Input] Keys: 0x04A0                 ← Gray
```

### VGA On-Screen Display (OSD)

Press **F12** during gameplay to toggle:

```
┌──────────────────────┐
│ FPS:60  EMU:NES      │
│ DRAM:180KB PSRAM:3MB │
│ Frame:12345 Turbo:OFF│
└──────────────────────┘
```

### Memory Report

Printed at boot and after ROM load:

```
===== MEMORY REPORT =====
DRAM  free: 180240 bytes (min: 165000)
PSRAM free: 3845120 bytes (min: 2100000)
Total free: 4025360 bytes
=========================
```

### Disabling Debug for Release

For maximum performance and minimum code size:

```cpp
#define DEBUG_LEVEL 0
#define DEBUG_VGA_OSD 0
```

---

## Build Configuration

### Build Metrics

| Metric | Value |
|--------|-------|
| Flash Usage | ~1.92 MB / 3.14 MB (61%) |
| DRAM Usage | ~120 KB / 327 KB (36%) |
| Free DRAM | ~207 KB |
| PSRAM | ~4 MB (for ROM data, framebuffers, audio) |

### Compiler Settings

| Setting | Value |
|---------|-------|
| Board | `esp32:esp32:esp32wrover` |
| Partition | `huge_app` (3MB app / 1MB SPIFFS) |
| Flash Freq | 80 MHz |
| PSRAM | Enabled (auto-detected with WROVER) |
| Upload Speed | 921600 baud |

### Compile-Time Options

| Define | Default | Description |
|--------|---------|-------------|
| `ENABLE_WIFI` | `0` | WiFi file manager (adds ~31KB DRAM) |
| `DEBUG_LEVEL` | `3` | Debug verbosity (0-4) |
| `DEBUG_VGA_OSD` | `1` | VGA debug overlay |
| `AUDIO_RING_SIZE` | `4096` | Audio buffer samples |
| `MAX_ROMS` | `100` | Maximum ROM files in launcher |
| `MAX_FAVORITES` | `20` | Maximum favorite entries |
| `MAX_RECENT` | `10` | Maximum recent entries |

---

## Architecture

```
Retro-Gamer.ino          ← Main: hardware init, launcher UI, game loop
├── src/
│   ├── debug.h           ← Debug system (Serial + VGA OSD)
│   ├── nes_bridge.c/h    ← NES emulator bridge
│   ├── gb_bridge.c/h     ← Game Boy bridge
│   ├── sms_bridge.c/h    ← SMS/GG/SG-1000/ColecoVision bridge
│   ├── pce_bridge.c/h    ← PC Engine bridge
│   ├── snes_bridge.c/h   ← SNES bridge
│   ├── lynx_bridge.c/h   ← Atari Lynx bridge
│   ├── gw_bridge.c/h     ← Game & Watch bridge
│   ├── genesis_bridge.c/h ← Mega Drive bridge
│   ├── doom_bridge.c/h   ← DOOM bridge
│   ├── msx_bridge.c/h    ← MSX bridge (stub)
│   ├── cover_art.c/h     ← Cover art loader
│   ├── zip_loader.c/h    ← ZIP extraction
│   ├── wifi_manager.*    ← WiFi + HTTP server
│   ├── nes/              ← nofrendo NES core
│   ├── gnuboy/           ← gnuboy GB/GBC core
│   ├── smsplus/          ← SMS Plus core
│   ├── pce/              ← HuExpress PCE core
│   ├── snes9x/           ← snes9x SNES core
│   ├── handy/            ← Handy Lynx core
│   ├── gw/               ← Game & Watch core
│   ├── gwenesis/         ← Gwenesis Genesis core
│   └── prboom/           ← PrBoom DOOM engine
```

### Design Pattern

Each emulator uses a **bridge pattern**:

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  Retro-Gamer.ino │────▶│  xxx_bridge.c/h  │────▶│  Emulator Core   │
│  (Hardware HAL)  │     │  (Adapter Layer)  │     │  (Original Code) │
└──────────────────┘     └──────────────────┘     └──────────────────┘
      │                         │
      ├─ VGA (FabGL)           ├─ init()
      ├─ PS/2 Keyboard         ├─ load_rom()
      ├─ SD Card               ├─ run_frame()
      ├─ Audio DAC             ├─ set_input()
      └─ PSRAM                 ├─ get_framebuffer()
                               ├─ get_audio()
                               ├─ save_state()
                               └─ shutdown()
```

### Memory Strategy

- **DRAM** (~327KB): Code variables, FabGL buffers, stack
- **PSRAM** (~4MB): ROM data, framebuffers, audio ring, OPL tables, cover art
- Large allocations use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
- VGA display uses FabGL `VGA8Controller` (64-color, 1 byte/pixel)

---

## Retro-Go Compatibility

This project is fully compatible with the [retro-go](https://github.com/ducalex/retro-go) SD card structure:

- ROM paths: `/roms/{platform}/`
- Save paths: `/retro-go/saves/{emu}/`
- Save naming: `game.ext.sav0` through `.sav3`
- Config: `/retro-go/config/`
- Cover art: `/romart/{emu}/`
- WiFi config: Same `wifi.json` format

You can use the same SD card between retro-go and Retro-Gamer.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **Black/no VGA output** | Check VGA cable connection. Ensure monitor supports 640×480@60Hz. Verify GPIO pin connections. |
| **"PSRAM not found"** | Your board must have an ESP32-WROVER module (with PSRAM). Regular ESP32 is not supported. |
| **SD card not detected** | Format as FAT32 (not exFAT). Check card is ≤32GB. Verify SPI pins (CS=13, CLK=14, MOSI=12, MISO=35). |
| **Keyboard not working** | Must be PS/2 protocol (not USB-to-PS/2 adapter). Check CLK=33, DAT=32. Try a different keyboard. |
| **No audio** | Audio output is on GPIO 25 (internal DAC). Connect amplified speaker or headphones via resistor divider. |
| **ROM list empty** | ROMs must be in `/roms/{platform}/` directories. Check file extensions match the supported list. |
| **SNES very slow** | Expected — ESP32 at 240MHz struggles with SNES emulation. Use simple games. |
| **DOOM crashes** | Ensure you're using DOOM1.WAD (shareware) or a compatible IWAD. Large mod WADs may exceed memory. |
| **WiFi won't enable** | WiFi adds ~31KB DRAM. Must set `ENABLE_WIFI 1` and rename `wifi_manager.cpp.disabled`. |
| **Compile error: DRAM overflow** | Reduce `MAX_ROMS` or set `DEBUG_LEVEL 0`. Don't enable WiFi simultaneously with high debug. |
| **Cover art not showing** | Files must be `.art` format in `/romart/{emu}/`. File name must match ROM name. |

---

## Known Limitations

- **Save States**: Bridge stubs return "FAIL" — full state serialization requires per-core implementation
- **SNES Performance**: ~15-25 FPS due to ESP32 CPU limitations
- **WiFi**: Cannot be enabled by default (ESP32 SDK uses ~31KB non-relocatable DRAM)
- **MSX**: Stub implementation — requires user-provided BIOS files
- **VGA Color Depth**: 64 colors (R2G2B2) — adequate for retro systems but limited for DOOM
- **Audio**: 8-bit DAC output at 22050Hz — acceptable for retro audio but not high fidelity
- **No Bluetooth**: Bluetooth gamepad support is not implemented
- **Single Player Only**: No multiplayer/netplay support

---

## Credits & Licenses

**Retro-Gamer** is open-source software licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**.
Due to the inclusion of GPL-3.0 components (like FabGL, gwenesis, and gw-emulator) that are statically linked, the entire combined work must be distributed under the terms of the GPL-3.0.

### Emulator Cores

| Core | Origin | Author | License |
|------|--------|--------|---------|
| nofrendo | [retro-go](https://github.com/ducalex/retro-go) | Matthew Conte / ducalex | LGPL-2.0 |
| gnuboy | [retro-go](https://github.com/ducalex/retro-go) | ducalex | GPL-2.0+ |
| smsplus | [retro-go](https://github.com/ducalex/retro-go) | ducalex | GPL-2.0+ |
| pce-go | [retro-go](https://github.com/ducalex/retro-go) | ducalex | GPL-2.0+ |
| snes9x | [snes9x](https://github.com/snes9xgit/snes9x) | snes9x team | Custom (Non-Commercial) |
| handy | [retro-go](https://github.com/ducalex/retro-go) | K. Wilkins | zlib |
| gwenesis | [gwenesis](https://github.com/bzhxx/gwenesis) | bzhxx | GPL-3.0 |
| prboom | [prboom](https://prboom.sourceforge.net/) | prboom team | GPL-2.0+ |
| gw-emulator | [gw-emulator](https://github.com/bzhxx/gw-emulator) | bzhxx | GPL-3.0 & BSD-3-Clause |

*(Note: The inclusion of Snes9x with its Non-Commercial clause technically creates a restriction incompatible with the GPL for the combined work. If distributing this software commercially, Snes9x must be removed.)*

### Libraries

| Library | Author | License |
|---------|--------|---------|
| [FabGL](http://www.fabglib.org/) | Fabrizio Di Vittorio | GPL-3.0 |
| [ESP32 Arduino](https://github.com/espressif/arduino-esp32) | Espressif | LGPL-2.1 |

### Special Thanks

- [ducalex](https://github.com/ducalex) for the retro-go project and emulator core ports
- [Fabrizio Di Vittorio](http://www.fabglib.org/) for the FabGL library
- [Olimex](https://www.olimex.com/) for the ESP32-SBC-FabGL board

---

*Built with ❤️ for retro gaming enthusiasts*
