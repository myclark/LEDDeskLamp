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
│   │   ├── main.cpp            # Main entry point and callbacks
│   │   ├── led_control.cpp     # LED state machine and brightness control
│   │   ├── touch_input.cpp     # Touch button debouncing and detection
│   │   └── battery_monitor.cpp # Battery voltage monitoring and state machine
│   ├── include/
│   │   ├── config.h            # All configuration defines
│   │   ├── led_control.h       # LED control interface
│   │   ├── touch_input.h       # Touch input interface
│   │   └── battery_monitor.h   # Battery monitoring interface
│   ├── test/
│   │   └── test_battery_state_machine.cpp  # Unit tests for battery state machine
│   └── platformio.ini          # Build configuration
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
- OFF/ON state model with MODE_WARM/MODE_COOL selector
- Per-mode brightness memory
- Smooth crossfade transitions (400ms) between modes and on mode swap
- ESP32 LEDC hardware PWM control (5kHz, 8-bit)
- Boundary detection with double-flash feedback
- Non-blocking battery indicator pulse animation (sine-envelope, configurable sharpness)

**touch_input module** - Capacitive touch handling:
- 50ms debouncing
- Multi-tap gesture detection: single, double, triple tap (300ms window)
- 800ms long press with start/hold/end callbacks
- Continuous brightness adjustment every 30ms while held
- Direction reversal on long press release
- Touch blocking during battery indicator animation

**battery_monitor module** - Battery voltage monitoring:
- ADC reading with 8-sample averaging for stability
- State machine with hysteresis (NORMAL → LOW → CRITICAL → CUTOFF)
- Configurable voltage thresholds
- Brightness limiting in CRITICAL state (50% max)
- Visual low-battery warnings via LED flashing

**main.cpp** - Setup, loop, and callbacks:
- RTC memory persistence (savedMode, warmBrightness, coolBrightness survive deep sleep)
- WiFi/Bluetooth disabled for power savings
- Deep sleep after 60s in OFF state
- Wake from deep sleep via GPIO3 (touch) — restores last mode and brightness
- Minimal loop delay (1ms) for smooth transitions

### State Machine Design

The lamp has two states (OFF / ON) with gesture-based control:

| Gesture | When OFF | When ON |
|---------|----------|---------|
| Single tap | Turn on (restore last mode + brightness) | Turn off |
| Double tap | Ignored | Swap WARM ↔ COOL (with crossfade) |
| Long press | Ignored | Adjust brightness (per-mode, direction reverses on release) |
| Triple tap | Ignored | Show battery level pulse indicator |

- On wake from deep sleep: treated as single tap (turn on, restore last state)
- Auto battery indicator plays after turn-on when battery is LOW or CRITICAL

### Current Implementation Status

**Fully Implemented:**
- ✅ Modular code structure (config, led_control, touch_input, battery_monitor, main)
- ✅ Touch input debouncing (50ms) and long press detection (800ms)
- ✅ Multi-tap gesture detection (single/double/triple tap with 300ms window)
- ✅ State machine (OFF/ON with MODE_WARM/MODE_COOL selector)
- ✅ Continuous gamma-corrected brightness control (not discrete steps)
- ✅ Per-mode brightness memory (WARM and COOL each remember their own level)
- ✅ Smooth crossfade transitions on tap and mode swap (400ms)
- ✅ ESP32 LEDC PWM control for flicker-free dimming
- ✅ Minimum brightness enforcement (never fully off when in ON state)
- ✅ Boundary detection with double-flash feedback
- ✅ Deep sleep mode after 60s in OFF state (~26µA)
- ✅ Wake from deep sleep on touch (restores last mode + brightness via RTC memory)
- ✅ WiFi and Bluetooth disabled for power savings
- ✅ Debug output with compile-time control (DEBUG flag)
- ✅ Battery voltage monitoring with ADC calibration
- ✅ Battery state machine with hysteresis (NORMAL/LOW/CRITICAL/CUTOFF)
- ✅ Non-blocking pulse battery indicator (sine envelope, sharpness encodes urgency)
- ✅ Auto battery warning indicator on turn-on when LOW or CRITICAL
- ✅ Manual battery indicator via triple tap (any battery level)
- ✅ Brightness limiting in CRITICAL state (50% max)
- ✅ Hard cutoff protection (refuses to turn on below 3.0V)
- ✅ Unit tests for battery state machine (9 tests)

**Known Issues & Fixes:**
- ✅ **LED ghosting in OFF state**: PWM=0 may not guarantee 0V on GPIO
  - Fix: Detach PWM and force GPIO LOW when in OFF state
  - Re-attach PWM when transitioning from OFF to ON

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

**State machine with hysteresis:** Prevents flickering between states due to voltage sag under load.

| State | Voltage Range | Behavior |
|-------|---------------|----------|
| NORMAL | > 3.5V | Full operation |
| LOW | 3.2V - 3.5V | Flash warning on wake only |
| CRITICAL | 3.0V - 3.2V | Flash every mode change, brightness limited to 50% |
| CUTOFF | < 3.0V | Refuse to turn on, enter deep sleep |

**Hysteresis logic:**
- NORMAL → LOW: Immediate at 3.5V
- LOW → CRITICAL: Requires 3 consecutive readings below 3.2V (90 seconds)
- CRITICAL → LOW: Requires voltage above 3.3V (100mV hysteresis)
- CUTOFF → CRITICAL: Requires voltage above 3.2V (200mV hysteresis, typically charging)

**ADC configuration:**
- 12-bit resolution (0-4095)
- 11dB attenuation for 0-3.3V range
- 8-sample averaging for stability
- Calibration factor (ADC_CALIBRATION_FACTOR) tuned to oscilloscope measurements
- BMS voltage drop compensation (BMS_VOLTAGE_DROP) for TP4056 MOSFET

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

// Battery voltage calibration
#define ADC_CALIBRATION_FACTOR 0.904  // Tuned to oscilloscope reading
#define BMS_VOLTAGE_DROP 0.090        // TP4056 MOSFET voltage drop (~90mV)

// Brightness (0-255)
#define MAX_BRIGHTNESS 128           // Set to 255 for full battery operation
#define MIN_BRIGHTNESS_PWM 1         // Minimum PWM output (prevents completely off)
#define GAMMA_CORRECTION 2.2         // Perceptual brightness curve

// Timing (milliseconds)
#define DEBOUNCE_MS 50               // Touch debounce time
#define LONG_PRESS_MS 800            // Time to trigger long press
#define BRIGHTNESS_STEP_MS 30        // How fast brightness changes when holding
#define MODE_TRANSITION_MS 400       // Crossfade duration between modes
#define DEEP_SLEEP_TIMEOUT_MS 60000  // Time in OFF before deep sleep
#define GESTURE_WINDOW_MS 300        // Max time between taps in a multi-tap sequence

// LED modes
#define MODE_WARM 0
#define MODE_COOL 1

// Battery indicator pulse (per state: count, period ms, sharpness)
#define PULSE_MIN_BRIGHTNESS 64      // Minimum indicator brightness (~25%)
#define PULSE_NORMAL_COUNT 3         // 3 slow pulses = full battery
#define PULSE_NORMAL_PERIOD_MS 800
#define PULSE_NORMAL_SHARPNESS 1.0
#define PULSE_LOW_COUNT 2            // 2 medium pulses = low battery
#define PULSE_LOW_PERIOD_MS 500
#define PULSE_LOW_SHARPNESS 2.0
#define PULSE_CRITICAL_COUNT 1       // 1 sharp pulse = critical
#define PULSE_CRITICAL_PERIOD_MS 300
#define PULSE_CRITICAL_SHARPNESS 5.0

// Debug
#define DEBUG 1                      // Set to 0 to disable all Serial output
```

### RTC Memory Persistence

Variables declared with `RTC_DATA_ATTR` survive deep sleep (lost on battery disconnect):

```cpp
RTC_DATA_ATTR uint8_t savedMode = MODE_WARM;   // Last active mode
RTC_DATA_ATTR uint8_t warmBrightness = 128;    // Warm mode brightness (default 50%)
RTC_DATA_ATTR uint8_t coolBrightness = 128;    // Cool mode brightness (default 50%)
RTC_DATA_ATTR uint16_t bootCount = 0;          // Debug: increments each wake/reset
```

On wake: `savedMode` and the corresponding brightness are restored via `turnOn()`. On mode swap or brightness change: the relevant RTC variable is updated immediately. Defaults (WARM, 50% each) apply after battery disconnect.

## Reference Documentation

See `doc/desk_lamp_project.md` for:
- Complete wiring diagram
- Detailed component specifications
- Power budget calculations
- Safety considerations
- Future enhancement ideas
- Bill of materials
