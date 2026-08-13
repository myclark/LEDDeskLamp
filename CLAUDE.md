# CLAUDE.md

ESP32-C3 desk lamp rebuild — potentiometer or physical-button control, dual-colour LEDs, Li-ion battery management.
See `doc/firmware_architecture.md` for detailed implementation notes.

## Dev Commands

```bash
cd firmware
pio run                                  # Build
pio run -t upload                        # Flash
pio device monitor                       # Serial monitor (115200 baud)
pio run -t upload && pio device monitor  # Flash + monitor
pio test -e native                       # Run native unit tests (44 tests, no hardware needed)
```

**IDE note:** Clang errors about `Arduino.h`, `millis()`, `HIGH` etc. are expected — ESP32 symbols are only visible to PlatformIO, not the IDE analyser.

**Test gotcha:** `test_touch` calls `updateButton()` before `initTouch()` in setUp. Static vars in `touch_input.cpp` must have safe defaults at declaration, not just inside `initTouch()`.

## Critical Hardware Config

ESP32-C3 SuperMini uses built-in USB — these flags in `platformio.ini` are **required** for Serial:
```ini
build_flags = -D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1
```

**Pin mapping (this is a running physical build — GPIO3/5/8/9/10/0 are already hardwired and must never be reassigned; new components only ever go on genuinely free pins):** GPIO3 = LIS3DH accelerometer interrupt (mode-swap gesture; also the deep-sleep wake pin in pot mode) — already wired here from before the pot/button work, unchanged. GPIO1 = primary on/off + brightness control — potentiometer wiper (`POT_PIN`, default) or physical button (`BUTTON_PIN`), only one wired up at a time per `USE_POT_INPUT` in `config.h`; GPIO1 was free and physically accessible, so that's where the new component goes, deliberately not GPIO3. GPIO10 = white LED (PWM), GPIO5 = warm LED (PWM), GPIO0 = battery ADC, GPIO8/GPIO9 = I2C SDA/SCL — all already wired, unchanged. GPIO4 = pot power switch (`POT_POWER_PIN`, pot mode only, also newly free).
**GPIO9 is a strapping pin** — avoid it (causes LED glow during sleep/programming).

## Architecture

All config in `include/config.h`. Modules are decoupled via callbacks; `main.cpp` only registers callbacks and runs the loop.

| Module | Responsibility |
|--------|---------------|
| `config.h` | All constants: pins, timing, battery thresholds, brightness, pulse params |
| `led_control` | OFF/ON state, gamma-corrected PWM, non-blocking crossfade + flash animations, brightness slew (pot mode) |
| `pot_input` | Potentiometer mode: ADC → brightness mapping, on/off hysteresis (pure/testable) |
| `touch_input` | Button mode: hardware-agnostic input → gesture decoder (single/double/triple tap, long press) |
| `battery_monitor` | ADC averaging, state machine (NORMAL/LOW/CRITICAL/CUTOFF), brightness limiting |
| `main.cpp` | Callbacks/pot polling, RTC persistence, deep sleep, auto-off timeout |

**Primary input is a compile-time choice:** `USE_POT_INPUT` in `config.h` selects potentiometer (default) or physical button — they share the same GPIO (`POT_PIN`/`BUTTON_PIN`, both GPIO1), only one is ever wired up. In button mode, `registerInputReader(fn)` swaps the raw input source behind the `touch_input.cpp` gesture engine without touching gesture logic (`injectInputEvent(bool)` for event-based sensors). Pot mode bypasses that gesture engine entirely — `pot_input.cpp` is polled directly every loop. The accelerometer never goes through either path — it's wired directly in `main.cpp` as an auxiliary mode-swap trigger in both modes. See `doc/firmware_architecture.md` for details.

**Deep sleep wake:** `BUTTON_PIN HIGH` in button mode; `LIS3DH_INT_PIN HIGH` in pot mode (the accelerometer is the *only* wake source once there's no button — enforced by a `#error` in `config.h` if `USE_POT_INPUT` is defined without `USE_ACCEL_INPUT`). Must update `enterDeepSleep()` in `main.cpp` if input hardware changes.

## Gesture / Control → Action

**Pot mode (default):**

| Input | OFF | ON |
|-------|-----|----|
| Turn pot | Turning above the on-threshold turns on at that brightness | Brightness tracks pot position live (eased ramp); turning to/below the off-threshold turns off |
| Tap lamp body `ACCEL_MODE_SWAP_TAP_COUNT` times (accelerometer, default 2 = double tap) | No effect | Swap WARM ↔ COOL (crossfade) |

Any other tap count on the lamp body, including a single tap, is ignored — not just while OFF. This is an exact match, not "2 or more": turning the pot knob shakes the same enclosure the accelerometer is mounted to, so requiring an exact count keeps ordinary brightness adjustment from randomly swapping modes, and deliberately leaves the *other* of {double, triple} tap free for a future gesture (`ACCEL_MODE_SWAP_TAP_COUNT` in `config.h`, must be 2 or 3).

Brightness is never persisted — it's always just wherever the pot currently points. Battery indicator shows automatically on wake/turn-on when LOW/CRITICAL, then recurs periodically while ON (`BATTERY_INDICATOR_REPEAT_LOW_MS`/`_CRITICAL_MS`, immediately again if it gets worse); there's no on-demand gesture for it.

**Button mode:**

| Gesture | OFF | ON |
|---------|-----|----|
| Single tap | Turn on (restore last mode + brightness) | Turn off |
| Double tap | — | Swap WARM ↔ COOL (crossfade) |
| Long press | — | Adjust brightness (direction reverses on release) |
| Triple tap | — | Show battery level pulse |

Tapping the lamp body `ACCEL_MODE_SWAP_TAP_COUNT` times (LIS3DH accelerometer, optional in button mode — `USE_ACCEL_INPUT`) is an additional trigger for the WARM ↔ COOL swap only; any other count, including a single tap, is ignored.

Auto-off after `AUTO_OFF_TIMEOUT_MS` (default 4 h) of no interaction → then deep sleep after `DEEP_SLEEP_TIMEOUT_MS` (60 s). In pot mode, only pot movement past `POT_MOVEMENT_DEADBAND` counts as interaction for the auto-off timer.

## Key Config (`include/config.h`)

```cpp
#define USE_POT_INPUT                // Comment out to use the physical button instead
#define MAX_BRIGHTNESS 255           // Reduce if testing on USB (not full battery)
#define DEBUG 0                      // Set to 1 to enable Serial output
#define AUTO_OFF_ENABLED 1
#define AUTO_OFF_TIMEOUT_MS 14400000 // 4 hours
#define DEEP_SLEEP_TIMEOUT_MS 60000  // 60 seconds in OFF before sleep
#define ADC_CALIBRATION_FACTOR 0.904 // Tune to match oscilloscope reading
#define BMS_VOLTAGE_DROP 0.090       // TP4056 MOSFET drop (~90 mV)
#define WATCHDOG_TIMEOUT_MS 8000     // Reboots if loop() stalls this long (see Timeout Audit & Watchdog in doc/firmware_architecture.md)
#define BRIGHTNESS_SLEW_TIME_CONSTANT_MS 150  // Pot mode: eased brightness follow speed (output)
#define POT_FILTER_TIME_CONSTANT_MS 30  // Pot mode: raw ADC noise filter, all positions (input)
#define POT_OFF_THRESHOLD 8          // Pot mode: brightness units at/below which lamp turns off
#define POT_ON_HYSTERESIS 13         // Pot mode: brightness units at/above which lamp turns on
#define POT_MAX_DEADZONE 8           // Pot mode: snaps to MAX_BRIGHTNESS within this margin of full scale
#define BATTERY_INDICATOR_REPEAT_LOW_MS      (20UL*60*1000)  // Recurring reminder while ON + LOW
#define BATTERY_INDICATOR_REPEAT_CRITICAL_MS (5UL*60*1000)   // Recurring reminder while ON + CRITICAL
#define ACCEL_MODE_SWAP_TAP_COUNT 2  // Exact tap count that swaps mode (2 or 3); the other is free for a future gesture
```

Battery thresholds (`BATTERY_LOW_THRESHOLD`, `BATTERY_CRITICAL_THRESHOLD`, etc.) and all pulse animation params are also in `config.h`.
