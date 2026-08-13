# CLAUDE.md

ESP32-C3 desk lamp rebuild — capacitive touch control, dual-colour LEDs, Li-ion battery management.
See `doc/firmware_architecture.md` for detailed implementation notes.

## Dev Commands

```bash
cd firmware
pio run                                  # Build
pio run -t upload                        # Flash
pio device monitor                       # Serial monitor (115200 baud)
pio run -t upload && pio device monitor  # Flash + monitor
pio test -e native                       # Run native unit tests (26 tests, no hardware needed)
```

**IDE note:** Clang errors about `Arduino.h`, `millis()`, `HIGH` etc. are expected — ESP32 symbols are only visible to PlatformIO, not the IDE analyser.

**Test gotcha:** `test_touch` calls `updateButton()` before `initTouch()` in setUp. Static vars in `touch_input.cpp` must have safe defaults at declaration, not just inside `initTouch()`.

## Critical Hardware Config

ESP32-C3 SuperMini uses built-in USB — these flags in `platformio.ini` are **required** for Serial:
```ini
build_flags = -D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1
```

**Pin mapping:** GPIO3 = physical on/off button, GPIO10 = white LED (PWM), GPIO5 = warm LED (PWM), GPIO0 = battery ADC, GPIO4 = LIS3DH accelerometer interrupt (mode-swap gesture only), GPIO8/GPIO9 = I2C SDA/SCL.
**GPIO9 is a strapping pin** — avoid it (causes LED glow during sleep/programming).

## Architecture

All config in `include/config.h`. Modules are decoupled via callbacks; `main.cpp` only registers callbacks and runs the loop.

| Module | Responsibility |
|--------|---------------|
| `config.h` | All constants: pins, timing, battery thresholds, brightness, pulse params |
| `led_control` | OFF/ON state, gamma-corrected PWM, non-blocking crossfade + flash animations |
| `touch_input` | Hardware-agnostic input → gesture decoder (single/double/triple tap, long press) |
| `battery_monitor` | ADC averaging, state machine (NORMAL/LOW/CRITICAL/CUTOFF), brightness limiting |
| `main.cpp` | Callbacks, RTC persistence, deep sleep, auto-off timeout |

**Input abstraction:** `registerInputReader(fn)` swaps the raw input source behind the gesture engine (currently the physical button on `BUTTON_PIN`) without touching gesture logic. Use `injectInputEvent(bool)` for event-based sensors. The accelerometer no longer goes through this path — it's wired directly in `main.cpp` as an auxiliary mode-swap trigger only. See `doc/firmware_architecture.md` for integration patterns.

**Deep sleep wake:** `BUTTON_PIN HIGH` — the accelerometer is not a wake source. Must update `enterDeepSleep()` in `main.cpp` if input hardware changes.

## Gesture → Action

| Gesture | OFF | ON |
|---------|-----|----|
| Single tap | Turn on (restore last mode + brightness) | Turn off |
| Double tap | — | Swap WARM ↔ COOL (crossfade) |
| Long press | — | Adjust brightness (direction reverses on release) |
| Triple tap | — | Show battery level pulse |

All gestures above are on the physical button. A tap on the lamp body (LIS3DH accelerometer, optional — `USE_ACCEL_INPUT`) is an additional trigger for the WARM ↔ COOL swap only.

Auto-off after `AUTO_OFF_TIMEOUT_MS` (default 4 h) of no interaction → then deep sleep after `DEEP_SLEEP_TIMEOUT_MS` (60 s).

## Key Config (`include/config.h`)

```cpp
#define MAX_BRIGHTNESS 255           // Reduce if testing on USB (not full battery)
#define DEBUG 0                      // Set to 1 to enable Serial output
#define AUTO_OFF_ENABLED 1
#define AUTO_OFF_TIMEOUT_MS 14400000 // 4 hours
#define DEEP_SLEEP_TIMEOUT_MS 60000  // 60 seconds in OFF before sleep
#define ADC_CALIBRATION_FACTOR 0.904 // Tune to match oscilloscope reading
#define BMS_VOLTAGE_DROP 0.090       // TP4056 MOSFET drop (~90 mV)
#define WATCHDOG_TIMEOUT_MS 8000     // Reboots if loop() stalls this long (see Timeout Audit & Watchdog in doc/firmware_architecture.md)
```

Battery thresholds (`BATTERY_LOW_THRESHOLD`, `BATTERY_CRITICAL_THRESHOLD`, etc.) and all pulse animation params are also in `config.h`.
