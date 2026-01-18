# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LED desk lamp rebuild using salvaged LED assembly with new electronics. The original lamp had failed charging circuitry. This project creates a reliable, long-lasting lamp with better battery life, low-battery warnings, and capacitive touch control.

**Hardware:** ESP32-C3 SuperMini, TP4056 USB-C charger, 18650 Li-ion battery, TTP223 capacitive touch sensor, N-channel MOSFETs for LED control.

**Key Feature:** Battery connected to 5V input (using onboard regulator to provide stable 3.3V to ESP32, which accepts 3.0-5.5V input). LEDs powered directly from battery voltage for full brightness range.

## Repository Structure

```
.
├── firmware/          # PlatformIO project for ESP32-C3
│   ├── src/
│   │   ├── main.cpp         # Main entry point and callbacks
│   │   ├── led_control.cpp  # LED state machine and brightness control
│   │   └── touch_input.cpp  # Touch button debouncing and detection
│   ├── include/
│   │   ├── config.h         # All configuration defines
│   │   ├── led_control.h    # LED control interface
│   │   └── touch_input.h    # Touch input interface
│   └── platformio.ini       # Build configuration
├── hardware/          # Hardware designs (to be added)
├── analysis/          # Power analysis and runtime prediction
│   ├── analyze_runtime.py   # Runtime analysis script
│   ├── templates/           # CSV templates for measurements
│   └── data/                # Measurement data files
└── doc/               # Project documentation
    └── desk_lamp_project.md  # Complete design document
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
- **GPIO10** - White LED control (PWM via LEDC channel 0)
- **GPIO5** - Warm LED control (PWM via LEDC channel 1)
- **GPIO0** - Battery voltage monitoring (ADC1_CH0, via voltage divider)
- **GPIO8** - Onboard LED (used for testing, active low)

**Important:** GPIO9 is a strapping pin and causes issues (LED glows during sleep). Use GPIO5 instead for warm LED.

## Architecture Overview

### Modular Code Structure

The firmware is split into separate modules for maintainability:

**config.h** - All configuration constants:
- Pin definitions
- Brightness settings (MAX_BRIGHTNESS, MIN_BRIGHTNESS_PWM, GAMMA_CORRECTION)
- Timing constants (debounce, long press, transitions, deep sleep)
- Debug output control (DEBUG flag)

**led_control module** - LED state machine and brightness:
- Continuous gamma-corrected brightness (0-128 range with LUT)
- Smooth crossfade transitions between modes (400ms)
- ESP32 LEDC hardware PWM control (5kHz, 8-bit)
- Boundary detection with double-flash feedback

**touch_input module** - Capacitive touch handling:
- 50ms debouncing
- 800ms long press detection
- Continuous brightness adjustment while holding
- Direction reversal at brightness boundaries

**main.cpp** - Setup, loop, and callbacks:
- WiFi/Bluetooth disabled for power savings
- Deep sleep after 60s in OFF state
- Wake from deep sleep via GPIO3 (touch)
- Minimal loop delay (1ms) for smooth transitions

### State Machine Design

The lamp operates in three states with touch-based cycling:
- **OFF** → **WHITE** → **WARM** → (back to OFF)
- **Tap:** Cycle to next state with smooth crossfade
- **Hold:** Continuously adjust brightness (gamma corrected)
- **Boundary:** Double flash when reaching min/max brightness

### Current Implementation Status

**Fully Implemented:**
- ✅ Modular code structure (config, led_control, touch_input, main)
- ✅ Touch input debouncing (50ms) and long press detection (800ms)
- ✅ State machine (OFF/WHITE/WARM with smooth transitions)
- ✅ Continuous gamma-corrected brightness control (not discrete steps)
- ✅ Smooth crossfade transitions between modes (400ms)
- ✅ ESP32 LEDC PWM control for flicker-free dimming
- ✅ Minimum brightness enforcement (never fully off when in ON state)
- ✅ Boundary detection with double-flash feedback
- ✅ Deep sleep mode after 60s in OFF state (~26µA)
- ✅ Wake from deep sleep on touch (goes directly to WHITE)
- ✅ WiFi and Bluetooth disabled for power savings
- ✅ Debug output with compile-time control (DEBUG flag)

**Known Issues & Fixes:**
- ✅ **LED ghosting in OFF state**: PWM=0 may not guarantee 0V on GPIO
  - Fix: Detach PWM and force GPIO LOW when in OFF state
  - Re-attach PWM when transitioning from OFF to WHITE/WARM

**TODO:**
- Battery voltage monitoring on GPIO0
- Low-battery warning (visual indication when < 3.5V)
- Adjust MAX_BRIGHTNESS from 128 to 255 for full battery voltage operation

### Brightness Control Implementation

**Gamma Correction:**
- Full lookup table (LUT) from 0-MAX_BRIGHTNESS
- Gamma = 2.2 for perceptual brightness
- MIN_BRIGHTNESS_PWM enforced (value of 1) to prevent completely off
- Entry [0] stays at 0 for OFF state transitions

**Continuous Dimming:**
- Hold button → brightness increments/decrements every 30ms
- Uses full brightness range (not discrete steps)
- Smoothly indexes into gamma LUT
- Direction reverses at boundaries (min/max)
- Double flash indicates boundary reached

**Mode Transitions:**
- Smooth 400ms crossfade when tapping between modes
- Linear interpolation between gamma-corrected PWM values
- Both LEDs updated simultaneously for seamless transitions
- Uses ESP32 LEDC for hardware PWM (5kHz, 8-bit resolution)

### Power Management

**Deep Sleep:**
- Enters deep sleep after 60 seconds in OFF state
- GPIO3 (touch) configured as wake source (ESP_GPIO_WAKEUP_GPIO_HIGH)
- **CRITICAL:** GPIO10/GPIO5 must use `gpio_hold_en()` to prevent LED glow during sleep
  - Without hold, GPIO leakage can partially turn on MOSFETs
  - Must call `gpio_hold_dis()` on wake to re-enable PWM control
- On wake, system resets and goes directly to WHITE mode
- Deep sleep current: ~10µA (ESP32-C3) + 1.5µA (TTP223) + 11µA (voltage divider) = ~22.5µA

**Power Optimization:**
- WiFi and Bluetooth disabled on startup (btStop(), WiFi.mode(WIFI_OFF))
- Direct LEDC PWM (no Arduino wrapper overhead)
- Minimal loop delay (1ms) for responsive transitions

### Battery Management

**Voltage divider:** 100kΩ (high) + 33kΩ (low) = high impedance to minimize drain (~11µA @ 3.7V)

**Critical thresholds:**
- 4.2V - Fully charged
- 3.7V - ~50% (nominal)
- 3.5V - Low battery warning
- 3.2V - Critical
- 3.0V - Hard cutoff (protect battery)

**ADC configuration (when implemented):**
```cpp
// ESP32-C3 ADC setup for battery monitoring
analogReadResolution(12);  // 12-bit ADC (0-4095)
// Read from GPIO0 (ADC1_CH0)
int adcValue = analogRead(BATTERY_PIN);
float voltage = adcValue * (3.3 / 4095.0) * (133.0 / 33.0);  // Voltage divider compensation
// Scaling factor: 4.030 (gives 1.042V at 4.2V battery)
```

### Power Budget
- **OFF (deep sleep):** ~22.5µA total → ~12 years standby (theoretical)
- **ON (full brightness at 50% PWM):** 60-90mA → 28-40 hours runtime (2500mAh battery)
- **ON (full brightness at 100% PWM):** 120-180mA → 14-20 hours runtime (when MAX_BRIGHTNESS = 255)

## Important Design Constraints

1. **Regulated power path:** Battery connects to 5V input, onboard regulator provides 3.3V to ESP32-C3 (chip operates 3.0-3.6V internally)
2. **LED power:** LED common V+ connected to battery voltage (via 5V input) for full brightness capability
3. **MOSFET gate resistors:** 120Ω series + 10kΩ pull-down on each gate (prevents floating during boot/reset)
4. **Touch module wake:** TTP223 active-high output enables deep sleep mode
5. **Common anode LEDs:** Common wire to battery V+, MOSFETs switch LED cathodes to ground
6. **High-Z voltage divider:** Minimizes parasitic drain during sleep (100kΩ + 33kΩ = 133kΩ total, ~11µA)
7. **GPIO9 is strapping pin:** Causes LED to glow during sleep/programming - use GPIO5 instead
8. **LEDC PWM required:** Direct LEDC control (not analogWrite) for smooth transitions

## Configuration Options

Key settings in `include/config.h`:

```cpp
// Pin assignments
#define TOUCH_PIN 3
#define WHITE_LED_PIN 10
#define WARM_LED_PIN 5
#define BATTERY_PIN 0

// Brightness (0-255)
#define MAX_BRIGHTNESS 128           // Set to 255 for full battery operation
#define MIN_BRIGHTNESS_PWM 1         // Minimum PWM output (prevents completely off)
#define GAMMA_CORRECTION 2.2         // Perceptual brightness curve

// Timing (milliseconds)
#define BRIGHTNESS_INCREMENT_MS 30   // How fast brightness changes when holding
#define MODE_TRANSITION_MS 400       // Crossfade duration between modes
#define DEEP_SLEEP_TIMEOUT_MS 60000  // Time in OFF before deep sleep

// Debug
#define DEBUG 1                      // Set to 0 to disable all Serial output
```

## Reference Documentation

See `doc/desk_lamp_project.md` for:
- Complete wiring diagram
- Detailed component specifications
- Power budget calculations
- Safety considerations
- Future enhancement ideas
- Bill of materials
