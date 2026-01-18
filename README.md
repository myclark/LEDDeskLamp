# LED Desk Lamp Rebuild

Rebuilt salvaged LED desk lamp with ESP32-C3, capacitive touch control, and improved battery management.

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

- **ESP32-C3 SuperMini** with deep sleep (~22.5µA standby)
- **Capacitive touch control** (TTP223) with state cycling
- **Smooth PWM dimming** via ESP32 LEDC (gamma corrected)
- **Battery monitoring** with low-battery warning
- **Safe power path** (battery → 5V input → onboard 3.3V regulator)
- **Continuous brightness control** (hold to adjust, boundary indication)

## Documentation

- **`CLAUDE.md`** - Quick reference for development (Claude Code context)
- **`doc/desk_lamp_project.md`** - Complete design document
- **`hardware/README.md`** - Schematic generation and PCB design notes
- **`analysis/README.md`** - Power analysis methodology

## Hardware Overview

**Core Components:**
- ESP32-C3 SuperMini
- TP4056 USB-C charger with BMS
- 18650 Li-ion battery (2500-3000mAh)
- TTP223 capacitive touch sensor
- 2× 2N7000 N-channel MOSFETs

**Power:**
- Standby: ~22.5µA (12+ years on 2500mAh battery)
- Active: ~80-90mA (28-30 hours runtime estimate)

## License

This is a personal project. Hardware designs and documentation are provided as-is.

Component datasheets and manufacturer specifications remain property of respective owners.

## Schematic Preview

Generate the schematic with `python hardware/schematic.py`, then view `hardware/schematic.svg`.

The schematic is code-based (Schemdraw) for easy version control and modification.
