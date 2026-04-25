# RC Controller Firmware Specifications

This document describes the current firmware behavior and architecture as implemented in `main/*`.

Last updated: April 25, 2026.

## 1. Purpose

The firmware turns an ESP32 into a configurable RC control hub:

- Input: Bluetooth gamepad (Bluepad32).
- Configuration: local web UI served by the ESP32 AP.
- Processing: virtual input mapping + output mixing.
- Output: PWM channels (servo/ESC style) and digital ON/OFF GPIO channels.

## 2. Platform and Runtime Stack

- Framework: ESP-IDF + Arduino as component.
- Core libraries/components:
  - Bluepad32 / btstack (Bluetooth gamepad stack)
  - ESP32Servo (PWM output driver)
  - WebServer + DNSServer (HTTP + captive portal)
  - SPIFFS VFS (frontend asset storage)
- Main loop responsibilities:
  1. Bluetooth update + scan scheduler.
  2. DNS/HTTP request processing.
  3. Control tick (input evaluation -> output write -> failsafe).

## 3. Current Code Architecture

The code has been split into focused modules:

- `sketch.cpp`: high-level orchestration, hardware/AP setup, API handlers.
- `web_routes.*`: centralized route registration.
- `state_service.*`: JSON payload construction (`/api/state`, `/api/activity`, inputs list) and standard API envelope.
- `json_utils.*`: lightweight JSON writer with shared escaping logic.
- `preset_service.*`: preset save/name utilities (user presets).
- `runtime_loop.*`: control tick + output count utility.
- `rc_model.*`: virtual input/output runtime logic and output hardware setup.
- `preset_store.*`: NVS persistence for presets/config/boot model.
- `web_ui.*`: filesystem mount + `index.html` streaming from SPIFFS.
- `data/index.html`: frontend UI asset.

## 4. Connectivity Requirements

### 4.1 Wi-Fi AP

- AP mode enabled on startup.
- Default SSID: `RC-Controller`.
- Default password: `rccontrol`.
- AP password rules:
  - empty string allowed (open AP), or
  - length 8..63.
- AP channel: 1.
- AP max clients: 4.

### 4.2 Captive Portal

- DNS wildcard redirect to AP IP.
- Common captive endpoints redirected to root (`/`), including:
  - `/generate_204`, `/gen_204`
  - `/hotspot-detect.html`, `/connecttest.txt`
  - `/ncsi.txt`, `/fwlink`
- Unknown GET routes are redirected to root.

### 4.3 Bluetooth Pairing/Scan

- One active gamepad at a time.
- Mouse/keyboard/balance board are ignored.
- Pairing window duration: 120 s.
- Scan behavior is pulsed:
  - auto reconnect mode: ON 1.5 s / OFF 12 s
  - pairing mode: ON 3.5 s / OFF 0.9 s
- Scan is forced off while a gamepad is connected.

## 5. Control Model

### 5.1 Virtual Inputs

- Max count: 16.
- Per-input configurable fields:
  - `name`
  - `primary`, `secondary`, `modifier`
  - `input_type` (`Direct`, `Toggle2Pos`, `Toggle3Pos`)
  - `range_mode` (`Unipolar`, `Bipolar`)
  - `modifier_function` (`None`, `Reverse`, `Center`)
  - `deadzone` (0..95)
  - `expo` (0..100)
  - `rumble` (bool)

Behavior summary:

- Direct:
  - normalized primary minus secondary (if configured),
  - deadzone + expo,
  - optional reverse via modifier.
- Toggle2Pos:
  - bipolar: -1 / +1
  - unipolar: 0 / +1
- Toggle3Pos:
  - bipolar: -1 / 0 / +1
  - unipolar: 0 / 0.5 / +1
- `Center` modifier on 3-position toggle forces center state.
- Rumble feedback is edge-triggered and rate-limited (80 ms minimum gap).

### 5.2 Outputs

- Max count: 24.
- Channel type:
  - PWM (50 Hz, 1000..2000 us, neutral 1500 us),
  - ON/OFF digital.
- Per-output fields:
  - `name`
  - `pin`
  - `type`
  - `inverted`
  - `threshold` (ON/OFF only)
  - `source_a/b/c` in `[-1..15]`
  - `mix_mode` (`Add`, `Multiply`)
  - `weight_*` and `offset_*` in `[-100..100]`

Signal formula:

- `term = source * (weight/100) + (offset/100)`
- Add mode: `A + B + C`
- Multiply mode: product of configured terms
- Final value clamped to `[-1..1]`, then inverted if enabled.

### 5.3 PWM Pin Compatibility and Remap

When loading presets across boards, output pins may be incompatible.

Current behavior:

- During preset apply/hardware rebuild, PWM outputs attempt requested pin first.
- If attach fails, firmware tries alternate PWM-capable pins not already used by another active output.
- If no valid pin can be attached, apply fails and previous config is restored.

### 5.4 Failsafe

- If no gamepad data is received for 450 ms:
  - PWM outputs -> neutral (1500 us),
  - switch outputs -> LOW.
- Failsafe is also applied when disconnected.

## 6. Presets and Persistence

### 6.1 Built-in Presets (read-only)

- `car`
- `excavator`
- `skid_steer`

### 6.2 User Presets

- Max count: 8.
- Name sanitization:
  - allowed chars: `[a-zA-Z0-9_-]`,
  - spaces replaced by `_`,
  - max effective length: 23.

Supported operations:

- apply preset,
- create preset (empty / clone / from current),
- save current preset,
- revert current preset,
- delete user preset,
- set boot default preset.

### 6.3 NVS Keys (namespace `rcctl`)

- `cfg`: current runtime config blob.
- `boot_model`: default preset name at boot.
- `preset_dir`: user preset directory.
- `pr0..pr7`: user preset blobs.
- `ap_ssid`, `ap_pass`: AP credentials.

Format guards:

- config magic: `0x52434346` (`RCCF`)
- config version: `5`
- preset directory version: `2`

## 7. HTTP API Contract

Base URL: ESP32 AP root.

### 7.1 UI and Captive

- `GET /` -> streams frontend `index.html` from SPIFFS.

### 7.2 State Endpoints

- `GET /api/inputs`
  - static input definitions for UI selectors.
- `GET /api/state`
  - full state/config payload (used for slower UI refresh).
- `GET /api/activity`
  - lightweight high-frequency payload:
    - virtual/output activity values,
    - top status chips fields (`ap_ssid`, `gamepad`, `bt_scan`, `pairing`).

### 7.3 Mutating Endpoints

- Virtual inputs:
  - `POST /api/virtual_add`
  - `POST /api/virtual_update`
  - `POST /api/virtual_delete`
- Outputs:
  - `POST /api/output_add`
  - `POST /api/output_update`
  - `POST /api/output_delete`
- Presets:
  - `POST /api/preset_apply`
  - `POST /api/model_set_default`
  - `POST /api/model_create`
  - `POST /api/model_save_current`
  - `POST /api/model_revert`
  - `POST /api/model_delete`
- AP config:
  - `POST /api/ap_config_set`
  - `POST /api/ap_config_apply_reboot`
- Bluetooth:
  - `POST /api/pairing_on`
  - `POST /api/pairing_off`
  - `POST /api/learn_detect`

## 8. Frontend Delivery and Refresh Strategy

### 8.1 Asset Delivery

- Frontend is stored in SPIFFS as `data/index.html`.
- Firmware streams file content in chunks (no monolithic HTML string allocation in RAM).

Deployment requirement:

- Flash both firmware and filesystem image:
  - `upload` (firmware)
  - `uploadfs` (SPIFFS image)

### 8.2 UI Refresh Model

- Full/static refresh (`/api/state`): every 2500 ms.
  - cards, presets, AP settings, metadata.
- Fast refresh (`/api/activity`): every 120 ms.
  - input/output bar activity,
  - top status chips (`Gamepad`, `Scan`, `Pairing`) and AP title.

This avoids rebuilding the full UI at high frequency while keeping live telemetry responsive.

## 9. Security and Operational Notes

- API is unauthenticated (local AP trust model).
- AP password is currently present in state payload for UI editing purposes.
- GPIO validity remains board-dependent even with pin checks/remap attempts.

## 10. Suggested Next Improvements

- Replace high-frequency polling with true push streaming (WebSocket/SSE) for activity/status.
- Add API auth mode for production deployments.
- Add automated tests for:
  - JSON payload structure,
  - preset apply rollback behavior,
  - pin remap selection logic.
