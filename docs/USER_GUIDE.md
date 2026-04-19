# RC Controller User Guide

This document explains how to use the web UI, configure inputs/outputs, and manage presets.

## 1. Overview

The firmware turns an ESP32 into a small RC control hub:

- A gamepad connects over Bluetooth (Bluepad32).
- The ESP32 exposes a Wi-Fi AP with a web UI.
- You map gamepad inputs to virtual inputs, then mix virtual inputs into hardware outputs.
- Outputs drive PWM channels (servo/ESC style) or ON/OFF pins.

Signal flow:

1. Gamepad input
2. Virtual input processing (type, deadzone, expo, modifiers)
3. Output mix (A/B/C, multiplier, offset, add/multiply mode)
4. GPIO output (PWM or ON/OFF)

## 2. Getting to the Web UI

1. Power the ESP32.
2. Connect your phone/PC to the ESP32 AP.
3. Open a browser:
4. If captive portal does not open automatically, browse to `192.168.4.1`.

## 3. Status Block

Shows:

- Gamepad connection state
- Bluetooth scan state
- Pairing state

Actions:

- `Pair 120s`: open pairing window
- `Stop Pairing`: stop pairing mode
- `AP Settings`: change AP SSID/password (and reboot if needed)

Screenshot (click to zoom):  
<a href="images/ui-overview.png"><img src="images/ui-overview.png" alt="Status block and overview" width="230"></a>

## 4. Model (Preset) Block

Use presets to store complete mappings.

- Built-in presets are readonly: `car`, `excavator`, `skid_steer`
- Custom presets are editable
- `Set as default` selects the boot preset

If you edit a readonly preset and press Save, the UI will ask to create a fork.

## 5. Preset Configuration

Two sections:

- Input Section (virtual inputs)
- Output Section (hardware channels)

Changes are staged in memory. Use:

- `Save` to store into the current preset
- `Revert` to discard unsaved changes

## 6. Input Configuration

Each virtual input has:

- `Name`
- `Input type`
- `Primary / Secondary / Modifier` source
- `Deadzone` and `Expo`
- Optional `Rumble`

Screenshot (click to zoom):  
<a href="images/ui-edit-input.png"><img src="images/ui-edit-input.png" alt="Edit Input modal" width="230"></a>

### Input types

- `Direct`: continuous value from sticks/triggers
- `2-position`: toggle state with button events
- `3-position`: three-state toggle

### Range mode

Available for `2-position` and `3-position`:

- `Bipolar`:
  - 2-position: `-100 / +100`
  - 3-position: `-100 / 0 / +100`
- `Unipolar`:
  - 2-position: `0 / 100`
  - 3-position: `0 / 50 / 100`

Notes:

- In Unipolar 3-position mode, startup state is `0`, so first press goes to `50`.
- `Modifier function` can be `Reverse` or `Center` depending on your mapping strategy.

### Rumble option

When enabled, the controller vibrates on input state transitions.

- 2-position and 3-position patterns are distinct by state.
- Intensity depends on controller support (some controllers are weaker than others).

## 7. Output Configuration

Each output channel has:

- `Name`
- `GPIO`
- `Type`: `PWM` or `ON/OFF`
- `Mix Mode`: `Add` or `Multiply`
- Sources `A/B/C`, each with:
  - `Mult %`
  - `Offset %`
- `Reversed`
- `Activation threshold` (ON/OFF only)

Screenshot (click to zoom):  
<a href="images/ui-edit-output.png"><img src="images/ui-edit-output.png" alt="Edit Output modal" width="230"></a>

### Math

Per source term:

`term = source_value * mult + offset`

Final output:

- `Add`: `termA + termB + termC`
- `Multiply`: `termA * termB * termC` (for defined terms)

Then clamped to valid range and optionally inverted.

This is useful for things like dual-rate scaling, mode-dependent limits, and advanced mixes.

## 8. Example: Steering Dual Rate

Typical approach:

1. Create an input `Steering DR` on a button (`3-position`, `Unipolar`, optional rumble).
2. In steering or throttle output (depending on your setup), set:
   - `Mix Mode = Multiply`
   - Source A = main command (`100%`)
   - Source B = DR input with `Mult = -75`, `Offset = 100`

That gives a scaling factor that changes with DR state.

## 9. AP Settings

From `AP Settings`:

- Change SSID/password
- `Save`: apply on next reboot
- `Apply & Reboot Now`: save and reboot immediately

If you manage several controllers, using unique AP names is recommended.

Screenshot (click to zoom):  
<a href="images/ui-ap-settings.png"><img src="images/ui-ap-settings.png" alt="AP Settings modal" width="230"></a>

## 10. Preset Errors and Corruption

Possible cases:

- `Preset obsolete (format changed after update)`: preset was created with an older incompatible schema.
- `Preset corrupted (...)`: stored blob is invalid.

You can delete invalid presets from the preset selector using `Delete Preset`.

## 11. Safety and Hardware Notes

- PWM outputs are servo-style (typically 1000-2000 us, 50 Hz).
- ON/OFF outputs are digital 3.3V logic.
- ESP32 GPIO pins are not power outputs for loads (LED strips, motors, etc.).
- Use proper drivers/MOSFETs/ESCs for external power loads.

## 12. Troubleshooting

- Cannot upload firmware:
  - Close serial monitor/tools holding the COM port.
- AP visible but page not opening:
  - Open `http://192.168.4.1`.
- Pairing issues:
  - Start `Pair 120s`, ensure controller is in pairing mode.
- Unexpected behavior after major update:
  - Rebuild or delete old custom presets and recreate from fresh built-ins.
