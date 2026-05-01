# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- (no entries yet)

## [0.2.0] - 2026-05-01

### Added
- Output-level PWM failsafe mode selection:
  - `Min`
  - `Center` (default)
  - `Max`
- Output activity bar now indicates failsafe visually:
  - bar position reflects configured failsafe PWM target
  - bar color switches to orange while failsafe is active
- VS Code project tasks for safer flashing:
  - `PIO: Upload All (esp32dev)` (`upload` + `uploadfs`)
  - `PIO: Upload All (esp32-s3-devkitc-1)` (`upload` + `uploadfs`)
- Firmware version visibility in UI status chips:
  - displays flashed firmware version and channel (`release`, `nightly`, `dev`)

### Changed
- Failsafe state is now propagated in state/activity API payloads to support
  explicit UI signaling.
- Nightly and release CI builds now inject `PROJECT_VER`, so flashed firmware
  reports coherent version identifiers in the UI.

### Fixed
- Output modal horizontal overflow in the web UI (fields no longer clip/overflow
  on the right side by a few pixels).

## [0.1.0] - 2026-05-01

### Added
- Virtual input modifier functions:
  - `Activate`: input is active only while modifier button is pressed.
  - `Desactivate`: input is disabled while modifier button is pressed.
- D-pad inputs are now available in input mapping:
  - `D-pad Up`, `D-pad Down`, `D-pad Left`, `D-pad Right`.
- Flashing docs now include release + nightly flow in English.
- Installer UI supports:
  - `Latest release`
  - `Nightly / specific tag`
- README compatibility table for tested boards:
  - ESP32-WROOM-32 (`esp32dev`)
  - ESP32-S3-DevKitC-1 (`esp32-s3-devkitc-1`)
- Tag-driven GitHub Release pipeline (`v*`) with:
  - firmware + filesystem build
  - bundle/meta/manifest generation
  - GitHub Release publication
  - GitHub Pages publication (`latest/`, `releases/<tag>/`, installer page)

### Fixed
- Nightly workflow summary for experimental targets fixed
  (`${{ matrix.env }}` interpolation in GitHub Actions).
- Installer now prioritizes `rc-controller-<board>-meta.json` and generates
  an ESP Web Tools manifest client-side to avoid CORS issues.
- Nightly pipeline publishes installer and release artifacts to GitHub Pages.
- Release export now uses per-environment flash offsets from `flash_args`
  (not hardcoded offsets), including S3 bootloader offset `0x0`.
- Release export regenerates bootloader/app binaries from ELF using
  flash parameters from `flash_args`, so image headers match board flash size.
- ESP32-S3 nightly/release flashing failures:
  - `invalid header: 0xffffffff`
  - `Detected size(4096k) smaller than the size in the binary image header(8192k)`
- Flashing guide installer URL is now clickable.

## [Project Initialization]

### Added
- Base firmware architecture for:
  - Bluepad32 controller input handling
  - Virtual input processing
  - Output mixing and GPIO/PWM outputs
  - Web UI and preset management
