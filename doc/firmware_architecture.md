# Firmware Architecture

Detailed implementation notes for the ESP32-C3 desk lamp firmware.

## Input Provider Abstraction

The gesture layer (`touch_input.cpp`) is decoupled from physical hardware via a function pointer:

```cpp
typedef bool (*InputStateReader)(void);
void registerInputReader(InputStateReader reader);
void injectInputEvent(bool pressed);  // for event-based sensors
```

**Default (TTP223):** reads `digitalRead(TOUCH_PIN) == HIGH`. Registered automatically in `initTouch()`.

**Button (active-low with pull-up):**
```cpp
bool buttonReader() { return digitalRead(BUTTON_PIN) == LOW; }
registerInputReader(buttonReader);
```

**Accelerometer (event-based):** accelerometers fire interrupts, not continuous state. Use `injectInputEvent()`:
```cpp
void IRAM_ATTR onTapDetected() {
  injectInputEvent(true);
  // call injectInputEvent(false) after ~DEBOUNCE_MS via timer or flag in loop()
}
```
`injectInputEvent()` auto-registers the injected-state reader.

**Deep sleep:** wake source is currently `TOUCH_PIN HIGH`. Changing input hardware requires updating `enterDeepSleep()` in `main.cpp` — the `esp_deep_sleep_enable_gpio_wakeup` call must match the new pin and polarity.

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
- Deep sleep: ~10 µA (ESP32-C3) + 1.5 µA (TTP223) + 11 µA (divider) ≈ 22.5 µA
- ON at 50% PWM: 60–90 mA → ~28–40 h runtime (2500 mAh)
- ON at 100% PWM: 120–180 mA → ~14–20 h runtime

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
