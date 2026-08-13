# Firmware Architecture

Detailed implementation notes for the ESP32-C3 desk lamp firmware.

## Input Provider Abstraction

The gesture layer (`touch_input.cpp`) is decoupled from physical hardware via a function pointer:

```cpp
typedef bool (*InputStateReader)(void);
void registerInputReader(InputStateReader reader);
void injectInputEvent(bool pressed);  // for event-based sensors
```

**Default (physical button):** reads `digitalRead(BUTTON_PIN) == HIGH`. Registered automatically in `initTouch()`. Wire the button between `BUTTON_PIN` and 3.3V with an external ~10kΩ pull-down to GND (idle LOW, pressed HIGH) — this is the same active-HIGH polarity the old TTP223 module used, so `touch_input.cpp` and its debounce/gesture engine are unchanged; only the physical part swapped.

**Accelerometer (auxiliary, mode-swap only):** the LIS3DH no longer goes through the `InputStateReader`/`injectInputEvent()` path at all. It never drove on/off — that always went through the button's own gesture engine — so `main.cpp`'s `updateAccelInput()` just watches `LIS3DH_INT_PIN` directly and calls `handleDoubleTap()` (mode swap) on every detected tap, with a cooldown to ignore ring-down re-triggers. See `doc/accel_input_integration.md`.

**Full gesture set lives on the button:** single tap (on/off), double tap (swap mode — also reachable via an accelerometer tap), long press (brightness), triple tap (battery indicator). The accelerometer is optional and additive; comment out `USE_ACCEL_INPUT` in `config.h` to run button-only, or remove the sensor entirely.

**Deep sleep:** wake source is `BUTTON_PIN HIGH` only — the accelerometer does not wake the device. Changing input hardware requires updating `enterDeepSleep()` in `main.cpp` — the `esp_deep_sleep_enable_gpio_wakeup` call must match the new pin and polarity.

## Brightness Control

**Gamma correction:** full LUT from 0–`MAX_BRIGHTNESS`, gamma = 2.2. `MIN_BRIGHTNESS_PWM` (= 1) prevents fully off while ON. LUT entry [0] = 0 for OFF transitions.

**Continuous dimming:** hold → increment/decrement every `BRIGHTNESS_STEP_MS` (30 ms). Direction reverses on release. Double-flash (non-blocking) on boundary hit.

**Mode crossfade:** 400 ms linear interpolation between gamma-corrected PWM values. Both LEDs updated simultaneously. Implemented as a non-blocking state machine in `updateModeTransition()`.

**Boundary flash:** was blocking (`delay(80)` × 3 = 240 ms, dropped touch events). Now non-blocking state machine — `triggerBoundaryFlash()` sets state; `updateModeTransition()` advances it. PWM writes in `incrementBrightness()` are skipped while `boundaryFlashActive`.

## Battery Management

**Voltage divider:** 100 kΩ + 33 kΩ (ratio 4.030, ~11 µA drain @ 3.7 V).

**ADC:** 12-bit, 11 dB attenuation, `ADC_SAMPLE_COUNT` (8) samples averaged. Calibrated via `ADC_CALIBRATION_FACTOR`; `BMS_VOLTAGE_DROP` compensates for TP4056 MOSFET.

**State machine with hysteresis:**

| State | Voltage | Behaviour |
|-------|---------|-----------|
| NORMAL | > 3.5 V | Full operation |
| LOW | 3.2–3.5 V | Warning pulse on wake/turn-on |
| CRITICAL | 3.0–3.2 V | Warning pulse on every turn-on, brightness capped at 50% |
| CUTOFF | < 3.0 V | Refuse to turn on, enter deep sleep |

Hysteresis: LOW→CRITICAL requires 3 consecutive readings (90 s); CRITICAL→LOW needs > 3.3 V; CUTOFF→CRITICAL needs > 3.2 V (typically charging).

**Battery indicator pulse:** non-blocking sine-envelope animation. Sharpness encodes urgency (1.0 = smooth sine, 5.0 = sharp spike). Blocks touch input while playing.

## Power Management

**Deep sleep:** entered after `DEEP_SLEEP_TIMEOUT_MS` (60 s) in OFF state. GPIO10/GPIO5 must use `gpio_hold_en()` before sleep to prevent MOSFET leakage causing LED glow; `gpio_hold_dis()` on wake.

**Auto-off:** after `AUTO_OFF_TIMEOUT_MS` (4 h) with no user interaction while ON, `turnOff()` is called; deep sleep timer then starts. `lastInteractionTime` is updated in every gesture callback and on wake from deep sleep.

**Estimated power:**
- Deep sleep: ~10 µA (ESP32-C3) + ~6 µA (LIS3DH, stays in low-power ODR mode for the mode-swap gesture) + 11 µA (divider) ≈ 27 µA — a physical button draws no standby current itself.
- ON at 50% PWM: 60–90 mA → ~28–40 h runtime (2500 mAh)
- ON at 100% PWM: 120–180 mA → ~14–20 h runtime

## Timeout Audit & Watchdog

Every `millis()`-based timer in the firmware (auto-off, deep-sleep entry, debounce, gesture
window, long-press, battery read/display intervals, boundary flash, mode transition, battery
indicator, accelerometer cooldown) uses the standard `millis() - lastEvent >= threshold`
idiom, which is safe across the `millis()` 32-bit rollover (~49.7 days) because unsigned
subtraction wraps correctly. No rollover bugs were found there.

The one real bug found was the deep-sleep timer's "not started" check: it used
`offStateStartTime == 0` as a sentinel, which is wrong at the exact instant `millis()` itself
returns 0 (very early boot). Fixed with an explicit `offTimerRunning` flag instead of an
overloaded zero value.

**Suspected cause of the reported freezes:** `Wire` (I2C) calls to the LIS3DH have no
timeout by default — if the bus glitches (e.g. from PWM switching noise) or `INT1` stays
latched from a failed read, `updateAccelInput()` can block on an I2C transaction every loop
iteration, stalling the entire `loop()` (and with it, gesture handling, auto-off, and deep
sleep) with no way to recover. Two mitigations:

1. `Wire.setTimeOut(I2C_TIMEOUT_MS)` (50 ms, see `config.h`) bounds every I2C transaction so
   a bus glitch fails fast instead of hanging.
2. **Task watchdog** (`esp_task_wdt`, see below) is the hard backstop for this and any other
   unforeseen stall.

### Task Watchdog Timer

The ESP32-C3 supports both a software **Task Watchdog Timer (TWDT)**, which monitors
whether subscribed FreeRTOS tasks check in periodically, and a hardware **RTC watchdog**,
which can catch failures even below the OS level (used mainly as a boot-loop safety net).
This firmware uses the TWDT: `initWatchdog()` in `main.cpp` subscribes the main loop task
with a timeout of `WATCHDOG_TIMEOUT_MS` (8 s, generous relative to the ~1 ms normal loop
period). `loop()` calls `esp_task_wdt_reset()` once per iteration to "pet" it. If `loop()`
ever fails to reach that call within the timeout — an I2C hang, or any future bug — the
watchdog panics and reboots the device instead of leaving it frozen. The TWDT API changed
shape between ESP-IDF 4 (`esp_task_wdt_init(timeout_s, panic)`) and ESP-IDF 5
(`esp_task_wdt_init(&config)`); `initWatchdog()` branches on `ESP_IDF_VERSION_MAJOR` to
support whichever Arduino core version is in use.

## RTC Memory Persistence

Survives deep sleep; lost on battery disconnect. Defaults (WARM, 50%) apply after disconnect.

```cpp
RTC_DATA_ATTR uint8_t savedMode      = MODE_WARM;
RTC_DATA_ATTR uint8_t warmBrightness = 128;
RTC_DATA_ATTR uint8_t coolBrightness = 128;
RTC_DATA_ATTR uint16_t bootCount     = 0;  // debug
```

## Hardware Design Constraints

- Battery → 5 V input → onboard regulator → 3.3 V ESP32 (chip range 3.0–3.6 V internally)
- LED common V+ from battery (not regulated), so full voltage drives LEDs
- MOSFET gates: 120 Ω series + 10 kΩ pull-down (prevents floating on boot/reset)
- Common-anode LEDs: MOSFETs switch cathodes to GND
- GPIO9 is a strapping pin — avoid (LED glows during sleep/programming); use GPIO5 for warm LED
- Use LEDC directly (not `analogWrite`) for smooth PWM transitions

## Testing

Native tests run on host (no hardware):
```bash
cd firmware && pio test -e native
```

Tests include `.cpp` source files directly (not via linking). The mock Arduino environment (`test/Arduino.h`, found via `-Itest` build flag) provides `millis()`, `delay()`, `digitalRead()`, `HIGH`/`LOW` etc.

**setUp() pattern:** `test_touch` flushes leftover state by calling `updateButton()` *before* `initTouch()`. Any static variable in `touch_input.cpp` used by `updateButton()` must have a safe non-null default at declaration.

| Suite | File | Tests |
|-------|------|-------|
| Touch gestures | `test/test_touch/test_touch_input.cpp` | 11 |
| Battery state machine | `test/test_battery/test_battery_state_machine.cpp` | 15 |
