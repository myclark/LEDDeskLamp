# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LED desk lamp rebuild using salvaged LED assembly with new electronics. The original lamp had failed charging circuitry. This project creates a reliable, long-lasting lamp with better battery life, low-battery warnings, and capacitive touch control.

**Hardware:** ESP32-C3 SuperMini, TP4056 USB-C charger, 18650 Li-ion battery, TTP223 capacitive touch sensor, N-channel MOSFETs for LED control.

**Key Feature:** Direct battery-to-MCU power (bypassing regulator) to use full battery capacity down to ~3.0V.

## Repository Structure

```
.
├── firmware/          # PlatformIO project for ESP32-C3
│   ├── src/
│   │   └── main.cpp   # Main firmware code
│   ├── lib/           # Custom libraries (if any)
│   ├── include/       # Header files
│   └── platformio.ini # Build configuration
├── hardware/          # Hardware designs, schematics, PCB files
└── doc/               # Project documentation
    └── desk_lamp_project.md  # Complete design document with pinouts, state machine, power budget
```

## Development Commands

### Building and Flashing
```bash
cd firmware
pio run              # Build firmware
pio run -t upload    # Upload to ESP32-C3 SuperMini
pio device monitor   # Open serial monitor (115200 baud)
```

### Combined Operations
```bash
pio run -t upload && pio device monitor  # Flash and monitor
```

## Critical Hardware-Specific Configuration

### ESP32-C3 SuperMini Build Flags

The ESP32-C3 SuperMini uses **built-in USB peripheral** (not external USB-to-serial). These flags in `platformio.ini` are **REQUIRED** for Serial output to work:

```ini
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
```

**Without these flags, Serial will not work.** The board configuration is `esp32-c3-devkitm-1` which is compatible with the SuperMini.

### Pin Mapping

Physical pin labels on the SuperMini board match GPIO numbers directly:
- **GPIO3** - Touch sensor input (TTP223 OUT)
- **GPIO8** - Onboard LED (also I2C SDA, active low)
- **GPIO9** - White LED control (PWM)
- **GPIO10** - Warm LED control (PWM)
- **A0** - Battery voltage monitoring (via voltage divider)

## Architecture Overview

### State Machine Design

The lamp operates in four states with touch-based cycling:
- **OFF** → **WHITE_FULL** → **WARM_FULL** → **BOTH_FULL** → (back to OFF)
- Long press (>800ms) from any ON state returns to OFF
- Short tap advances to next state

See `doc/desk_lamp_project.md` for complete state machine diagram.

### Current Implementation Status

**Implemented (in main.cpp):**
- Touch input debouncing (50ms threshold)
- Long press detection (800ms threshold)
- Callback-based event handling
- Serial debugging output

**TODO (see main.cpp comments):**
- State machine implementation (handleTap/handleLongPress functions)
- LED PWM control on GPIO9/10
- Battery voltage monitoring on A0
- Low-battery warning (3× pulse when entering ON state below 3.5V)
- Deep sleep mode in OFF state

### Battery Management

**Voltage divider:** 100kΩ (high) + 27kΩ (low) = high impedance to minimize drain (~15µA @ 3.7V)

**Critical thresholds:**
- 4.2V - Fully charged
- 3.7V - ~50% (nominal)
- 3.5V - Low battery warning
- 3.2V - Critical
- 3.0V - Hard cutoff (protect battery)

**ADC configuration needed:**
```cpp
analogReference(INTERNAL); // Use 1.1V internal reference for stable readings
```

### Power Budget
- **OFF (deep sleep):** ~26µA total → ~10 years standby (theoretical)
- **ON (full brightness):** 120-180mA → 14-20 hours continuous runtime (2500mAh battery)

## Important Design Constraints

1. **Direct battery power:** ESP32-C3 runs on battery voltage (2.3V-3.6V range), no regulator
2. **Touch module wake:** TTP223 active-high output enables deep sleep mode
3. **Common cathode LEDs:** MOSFETs switch LEDs to ground
4. **High-Z voltage divider:** Minimizes parasitic drain during sleep

## Reference Documentation

See `doc/desk_lamp_project.md` for:
- Complete wiring diagram
- Detailed component specifications
- Power budget calculations
- Safety considerations
- Future enhancement ideas
- Bill of materials
