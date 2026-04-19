# RC Controller (ESP32 + Bluepad32 + Web UI)

Web-configurable RC transmitter/mapper running on ESP32 boards.

This project uses:
- **ESP-IDF 5.4.2** (via PlatformIO)
- **Arduino as ESP-IDF component**
- **Bluepad32** for Bluetooth gamepad support
- **ESP32Servo** for PWM outputs

Main target currently used in development: **ESP32-S3-DevKitC-1**  
Planned/expected compatibility: **ESP32-WROOM-32** (including DualShock 4 over Bluetooth Classic).

## Features

- Wi-Fi AP + captive portal web interface
- Bluetooth gamepad pairing/scan controls
- Channel mapping UI (PWM / ON-OFF)
- Primary / Secondary / Modifier input model
- Live input activity indicators
- Presets (models):
  - Built-in readonly presets: `car`, `excavator`
  - Custom presets: create, edit, save, revert, delete
  - Boot default preset selection
  - Readonly fork flow (save on readonly opens fork/create flow)
- AP configuration from UI:
  - AP SSID
  - AP password
  - Save for next boot, or **Apply & Reboot Now**

## Repository structure

- `main/sketch.cpp`: runtime orchestration, web handlers, hardware glue
- `main/web_ui.cpp`: embedded frontend page (HTML/CSS/JS)
- `main/control_inputs.*`: gamepad input definitions + normalization + learn detection
- `main/preset_store.*`: preset/NVS persistence helpers
- `components/`: third-party components (e.g. ESP32Servo)
- `patches/`: local component patches

## Build and flash (PlatformIO)

```powershell
# Build (ESP32-S3)
pio run -e esp32-s3-devkitc-1

# Upload
pio run -e esp32-s3-devkitc-1 -t upload

# Serial monitor
pio device monitor -p COM8 -b 115200
```

If `pio` is not in your PATH on Windows, use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-s3-devkitc-1
```

## Notes

- Some boards report flash-size mismatch warnings in PlatformIO if board config and physical flash differ.
- Current AP/channel coexistence behavior is tuned for development and may still need per-board tuning.
- `car` and `excavator` are intentionally readonly baseline presets.

## AI-assisted development

This project is developed primarily with AI assistance (architecture, implementation, refactors, and UI iterations), with human review and iterative validation on hardware.

## Roadmap (short)

- Continue code split from `sketch.cpp` into dedicated modules
- Improve AP + BT coexistence across more ESP32 variants
- Preset import/export
- Better gamepad profile abstraction (labeling/layout per controller family)

## License

See [LICENSE](LICENSE).
