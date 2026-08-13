# LED Desk Lamp Rebuild

Rebuilt salvaged LED desk lamp with ESP32-C3, potentiometer or physical-button control, and improved battery management.

## Quick Start

### Hardware
See circuit diagrams in `doc/desk_lamp_project.md` (ASCII + Mermaid block diagrams).

### Firmware
Build and flash to ESP32-C3:
```bash
cd firmware
pio run -t upload
pio device monitor
```

See `CLAUDE.md` for detailed development instructions.

### Power Analysis
Measure and predict battery runtime:
```bash
cd analysis
python analyze_runtime.py data/your_measurement.csv
```

See `analysis/README.md` for measurement procedures.

## Project Structure

```
├── firmware/          # ESP32-C3 PlatformIO project
├── hardware/          # Schemdraw schematic generator
├── analysis/          # Power measurement and runtime prediction
└── doc/               # Complete design documentation
```

## Key Features

- **ESP32-C3 SuperMini** with deep sleep
- **Potentiometer control (default)** — turn to set brightness live, fully counter-clockwise to turn off, smooth eased tracking. A physical button (single/double/triple tap, long press) is available as a compile-time alternative — see `USE_POT_INPUT` in `firmware/include/config.h`.
- **Accelerometer mode-swap gesture** (LIS3DH) — a tap on the lamp body swaps WARM ↔ COOL in either input mode; it's also the deep-sleep wake source in potentiometer mode
- **Smooth PWM dimming** via ESP32 LEDC (gamma corrected)
- **Battery monitoring** with low-battery warning
- **Safe power path** (battery → 5V input → onboard 3.3V regulator)
- **Task watchdog + I2C timeout** — the device reboots itself if the main loop ever stalls (e.g. an I2C bus glitch) instead of freezing

## Documentation

- **`CLAUDE.md`** - Quick reference for development (Claude Code context)
- **`doc/firmware_architecture.md`** - Firmware implementation notes: input modes, timing, watchdog, power
- **`doc/desk_lamp_project.md`** - Complete design document
- **`doc/accel_input_integration.md`** - LIS3DH accelerometer integration notes
- **`hardware/README.md`** - Schematic generation and PCB design notes
- **`analysis/README.md`** - Power analysis methodology

## Hardware Overview

**Core Components:**
- ESP32-C3 SuperMini
- TP4056 USB-C charger with BMS
- 18650 Li-ion battery (2500-3000mAh)
- Potentiometer (default, 10kΩ linear taper) **or** physical momentary button — only one is ever wired up, selected at compile time by `USE_POT_INPUT`
- LIS3DH accelerometer — mode-swap tap gesture; also the deep-sleep wake source in potentiometer mode
- 2× 2N7000 N-channel MOSFETs

**Power (theoretical, component-datasheet based — not yet measured on real hardware; see `analysis/`):**
- Standby, both modes: ~21–27µA (~10.5+ years on 2500mAh, theoretical). In potentiometer mode this relies on `POT_POWER_PIN` switching the pot's own supply off during sleep — without it, a pot wired straight across 3.3V/GND would add ~330µA of its own and dominate the whole budget. See the Power Management section in `doc/firmware_architecture.md` for the full breakdown.
- Active: ~80-90mA (28-30 hours runtime estimate, 2500mAh)

## License

This is a personal project. Hardware designs and documentation are provided as-is.

Component datasheets and manufacturer specifications remain property of respective owners.

## Schematic Preview

Generate the schematic with `python hardware/schematic.py`, then view `hardware/schematic.svg`.

The schematic is code-based (Schemdraw) for easy version control and modification.
