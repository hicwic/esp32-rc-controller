# Flashing Guide (Minimal Tools)

This guide is intended for non-technical users.

## Supported Boards (End-User Releases)

- ESP32 Dev Module (`esp32dev`)
- ESP32-S3 DevKitC-1 (`esp32-s3-devkitc-1`)

Experimental (not released for end-users yet):
- ESP32-C3 DevKitC-02 (`esp32-c3-devkitc-02`)
- ESP32-C6 DevKitC-1 (`esp32-c6-devkitc-1`)

## 1. Recommended Method: Web Installer (Release or Nightly)

### What to install on PC

1. USB driver for your ESP32 board:
   - CP210x driver (Silicon Labs), or
   - CH340 driver (WCH), depending on your board USB chip.
2. A Chromium-based browser:
   - Google Chrome or Microsoft Edge.

No Python, PlatformIO, or terminal is required.

### Steps

1. Open the installer page:
   - `https://hicwic.github.io/esp32-rc-controller/install.html`
2. Select your board.
3. Select channel:
   - `Latest release` for normal end-user updates.
   - `Nightly / specific tag` for CI/nightly builds.
4. If you selected `Nightly / specific tag`, enter the release tag (example: `nightly-20260430-0220`).
5. Click **Connect**.
6. Select the correct serial port.
7. Click **Install**.
8. Wait until flashing completes, then reset/replug the board.

If connection fails:

- hold the board `BOOT` button while clicking Connect,
- then release when flashing starts.

### Where to find nightly tags

1. Open GitHub `Releases`.
2. Find the nightly entry you want.
3. Copy its tag name (format: `nightly-YYYYMMDD-HHMM`).
4. Paste it in the installer `Nightly / specific tag` field.

Note: the installer page reads firmware files from the same GitHub Pages site to avoid browser CORS issues.

## 2. Release Assets (What is flashed)

Each board release includes:

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `spiffs.bin` (web UI filesystem)
- `manifest-<board>.json` (used by the web installer)

Important: `spiffs.bin` is required, otherwise UI will show:
`Missing UI asset /index.html`.

## 3. Advanced Fallback (CLI)

For advanced users only:

- install Python + `esptool`
- flash all required binaries at their offsets
- include filesystem image (`spiffs.bin`)

For most users, prefer the web installer.

## 4. Current Limitation

There is currently no in-device OTA update button inside the RC Controller AP UI.
Use the web installer page over USB for both `release` and `nightly` flashing.
