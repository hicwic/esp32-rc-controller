# Flashing Guide (Minimal Tools)

This guide is intended for non-technical users.

## 1. Recommended Method: Web Installer

### What to install on PC

1. USB driver for your ESP32 board:
   - CP210x driver (Silicon Labs), or
   - CH340 driver (WCH), depending on your board USB chip.
2. A Chromium-based browser:
   - Google Chrome or Microsoft Edge.

No Python, PlatformIO, or terminal is required.

### Steps

1. Open the installer page: [Install Page](install.html)
2. Select your board.
3. Click **Connect**.
4. Select the correct serial port.
5. Click **Install**.
6. Wait until flashing completes, then reset/replug the board.

If connection fails:

- hold the board `BOOT` button while clicking Connect,
- then release when flashing starts.

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

