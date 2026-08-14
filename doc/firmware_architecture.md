# Firmware Architecture

Detailed implementation notes for the ESP32-C3 desk lamp firmware.

## Primary Input: Potentiometer or Button

`config.h`'s `USE_POT_INPUT` selects exactly one primary control for on/off + brightness.
Both options share the same physical GPIO (`POT_PIN`/`BUTTON_PIN`, both GPIO1) since only
one is ever wired up at a time. GPIO1 was chosen because it was genuinely free on the
existing board and physically accessible — this is a running physical build, and GPIO3
(the pin the old capacitive touch module used to share with the LIS3DH's INT1) is already
hardwired to the accelerometer alone at this point, so the new pot/button input
deliberately does not reuse it. The accelerometer's role (mode-swap gesture) is the same
either way — see the next section.

### Potentiometer mode (`USE_POT_INPUT` defined — the default)

`pot_input.cpp` reads the pot wiper every `loop()` iteration and treats its position as a
live, continuous request — there's no discrete "gesture" or debounce involved, and nothing
about brightness is persisted to RTC memory: the pot's current position *is* the requested
brightness, always.

- **Mapping:** raw ADC (0–4095 at 12-bit resolution) → brightness (0–`MAX_BRIGHTNESS`)
  linearly via `mapPotToBrightness()`.
- **On/off hysteresis (bottom of the dial):** two thresholds (`POT_OFF_THRESHOLD` ~3%,
  `POT_ON_HYSTERESIS` ~5%) prevent flicker right at the "off" end — see
  `updatePotStateMachine()` in `pot_input.cpp`.
- **Top dead zone:** mirrors the bottom at the other end — `mapPotToBrightness()` snaps
  anything within `POT_MAX_DEADZONE` (~3%) of full scale to exactly `MAX_BRIGHTNESS`, since
  a pot rarely hits its mechanical/electrical limit exactly and the user should still be
  able to reach 100% by feel. This one is a plain value clamp inside the mapping function
  itself, not a separate state machine — unlike the bottom, crossing it isn't a functional
  state change.
- **Whole-travel noise filtering:** `smoothPotReading()` applies exponential smoothing to
  the pot's reading every tick, before it feeds into either the on/off decision or the
  brightness target — this is distinct from (and happens *before*) the brightness slew
  below, and matters at every dial position, not just the two ends: without it, a single
  noisy ADC sample could transiently cross the on/off hysteresis boundary or produce a
  visible flicker in the middle of the travel. `POT_FILTER_TIME_CONSTANT_MS` (30 ms) is
  intentionally much shorter than the brightness slew's time constant — it exists to erase
  single-tick jitter, not to add perceptible lag.
- All three of `mapPotToBrightness()`, `updatePotStateMachine()`, and `smoothPotReading()`
  are pure functions with no hardware access, covered by `test/test_pot/`.
- **Brightness slew:** `main.cpp` calls `setBrightnessTarget()` with the live pot reading
  every tick; `led_control.cpp`'s `updateBrightnessSlew()` eases the actual PWM output
  toward that target with an exponential filter (`BRIGHTNESS_SLEW_TIME_CONSTANT_MS`), so
  fast pot movements produce a smooth, slightly-lagging ramp rather than an instant jump —
  turning the pot back and forth quickly still "tracks" the user, just softened.
- **Auto-off interaction:** only pot movement past `POT_MOVEMENT_DEADBAND` counts as
  interaction — this filters ADC jitter that would otherwise reset the auto-off timer
  forever (see Timeout Audit & Watchdog below for why that distinction matters).
- **Wake-then-check:** since the accelerometer (not the pot) wakes the device from deep
  sleep, a wake doesn't automatically mean "turn on" — it might just be a hand bumping the
  lamp while reaching for the dial. On `ESP_SLEEP_WAKEUP_GPIO`, `setup()` re-powers the pot
  (see below), waits `POT_SETTLE_MS` for the RC front end to settle, then takes a fresh pot
  reading and only turns on if the pot itself is requesting ON; otherwise the device stays
  OFF-but-awake and the normal deep-sleep timer puts it straight back to sleep, invisibly
  to the user.
- **`USE_POT_INPUT` requires `USE_ACCEL_INPUT`** — enforced with a `#error` in `config.h`.
  With no button, the accelerometer tap is the *only* way to wake the device; without it,
  the lamp would sleep forever once it entered deep sleep.

#### Hardware: pot + RC front end + switched power

Analog complement to the digital filtering above — see the `POT_PIN`/`POT_POWER_PIN`
comments in `config.h` for the same values inline with the pinout.

```
POT_POWER_PIN (GPIO4) ──┬──────────────┐
                         │              │
                       [ POT ]          │
                         │              │
                 wiper ──┴──[ R 1kΩ ]──┬──► POT_PIN (GPIO1)
                                        │
                                      [ C 1µF ]
                                        │
GND ──────────────────────────────────┴──── (pot's other outer leg also to GND)
```

- **Pot: 10kΩ linear taper.** Must be linear, not audio/log — `mapPotToBrightness()` is a
  straight linear map, so a log-taper pot would make brightness feel badly non-uniform
  across the travel. 10kΩ is chosen purely for ADC accuracy (low source impedance) — its
  ~330µA draw (3.3V / 10kΩ) is no longer a standby-power concern now that `POT_POWER_PIN`
  switches it off during sleep, so there's no reason to trade accuracy for a higher value.
- **Series R: 1kΩ**, wiper to `POT_PIN`. Keeps total source impedance (pot's own ~2.5kΩ
  worst case at mid-travel + this 1kΩ) comfortably under the ESP32 ADC's recommended limit
  for accurate 12-bit conversions.
- **Shunt C: 1µF ceramic (X7R, ≥6.3V)**, `POT_PIN` to GND. With R=1kΩ this gives a cutoff
  around 160 Hz — deliberately ~30x faster than `POT_FILTER_TIME_CONSTANT_MS` (so it
  doesn't add felt lag on top of the digital filter, just cleans up what reaches it) and
  ~30x below the 5 kHz LED PWM frequency (meaningful attenuation of that specific noise
  source, ~20 dB/decade past cutoff on a single-pole filter). The two filters are
  independent and can be retuned separately: raise C (e.g. 2.2–4.7µF) for more analog
  filtering, or lower `POT_FILTER_TIME_CONSTANT_MS` for more digital filtering.
- **Switched power, no transistor needed:** the pot's "3.3V" leg wires to `POT_POWER_PIN`
  (a GPIO) instead of the fixed rail. At the pot's ~330µA draw, the GPIO driver's own
  on-resistance drop is a few mV at most — well inside ESP32-C3's GPIO sourcing capability
  (tens of mA) — so driving the pin HIGH is electrically indistinguishable from tying it to
  3.3V directly; no external switch transistor is required. `pot_input.cpp`'s
  `potPowerOn()`/`potPowerOff()` are plain `digitalWrite()` calls. `POT_POWER_PIN` must be
  RTC-capable (GPIO0-5 on the ESP32-C3) so `gpio_hold_en()`/`gpio_hold_dis()` can latch it
  LOW through deep sleep — the same technique already used for the LED pins in
  `enterDeepSleep()`. `main.cpp` releases the hold *before* `initPotInput()` reconfigures
  the pin on wake (a held pin ignores `pinMode()`/`digitalWrite()` until released), then
  waits `POT_SETTLE_MS` (≈5x the RC front end's worst-case time constant, with margin)
  before the first reading is trusted — this covers both a deep-sleep wake and a fresh
  power-on, since `initPotInput()` (and thus `potPowerOn()`) always runs during `setup()`.

### Button mode (`USE_POT_INPUT` commented out)

The gesture layer (`touch_input.cpp`) is decoupled from physical hardware via a function pointer:

```cpp
typedef bool (*InputStateReader)(void);
void registerInputReader(InputStateReader reader);
void injectInputEvent(bool pressed);  // for event-based sensors
```

**Default (physical button):** reads `digitalRead(BUTTON_PIN) == HIGH`. Registered automatically in `initTouch()`. Wire the button between `BUTTON_PIN` and 3.3V with an external ~10kΩ pull-down to GND (idle LOW, pressed HIGH) — this is the same active-HIGH polarity the old TTP223 module used, so `touch_input.cpp` and its debounce/gesture engine are unchanged; only the physical part swapped.

**Full gesture set lives on the button:** single tap (on/off), double tap (swap mode — also reachable via an accelerometer tap), long press (brightness), triple tap (battery indicator).

### Accelerometer (auxiliary, mode-swap only, in both modes)

The LIS3DH never goes through the `InputStateReader`/`injectInputEvent()` path. It never
drove on/off — that always went through the button's own gesture engine, or now the pot's
state machine — so `main.cpp`'s `updateAccelInput()` just watches `LIS3DH_INT_PIN` directly.
Mode swap requires an **exact** `ACCEL_MODE_SWAP_TAP_COUNT` (config.h, default 2 — double
tap), not "2 or more": a lone tap is always ignored as an incidental bump (turning the pot
knob shakes the same enclosure the accelerometer is mounted to), and a count that overshoots
the configured value is discarded too, not treated as a match. This is deliberate, not just
an anti-bump measure — it leaves the *other* of {double, triple} tap free for a future
gesture without needing new hardware. A firmware-side gesture window counts taps before
dispatching `handleModeSwap()` (renamed from `handleDoubleTap()` since the accelerometer's
trigger count is no longer necessarily two). See `doc/accel_input_integration.md` for the
full state machine. The accelerometer is optional in button mode (comment out
`USE_ACCEL_INPUT` to run button-only — double-tapping the button still swaps modes, via the
same `handleModeSwap()`) but required in pot mode.

**Deep sleep wake source:** `BUTTON_PIN HIGH` in button mode, `LIS3DH_INT_PIN HIGH` in pot
mode — never both. Changing input hardware requires updating `enterDeepSleep()` in
`main.cpp` — the `esp_deep_sleep_enable_gpio_wakeup` call must match the new pin and polarity.

**Wake tap sensitivity vs. gesture tap sensitivity:** in pot mode the same LIS3DH click
engine that detects the awake double/triple-tap gesture also has to detect the tap that
wakes the device from deep sleep, but those two situations want different sensitivity.
`LIS3DH_CLICK_THS` (config.h) is deliberately conservative while awake, tuned to reject
incidental bumps — the lamp body gets knocked constantly by ordinary handling, especially
turning the pot knob. That same threshold is too insensitive for a deep-sleep wake tap,
where the goal is the opposite: even a gentle jostle should wake the device, since a false
wake costs a little battery (the device just checks the pot and, finding it still at OFF,
goes straight back to sleep — see "Wake-then-check" above) while a missed wake means
physically power-cycling the lamp. `enterDeepSleep()` reprograms the LIS3DH's `CLICK_THS`
register to the more sensitive `LIS3DH_WAKE_CLICK_THS` right before calling
`esp_deep_sleep_start()`, via `accelSetClickThreshold()` (`accel_input.cpp`) — a targeted
single-register write, not a full reinit. On the next boot, `accelInit()` unconditionally
reprograms every LIS3DH register including `CLICK_THS`, resetting it back to the normal
`LIS3DH_CLICK_THS` before `loop()` (and therefore any gesture detection) ever runs — so the
more sensitive wake threshold never leaks into normal double-tap operation, and no explicit
"restore" step is needed on the wake path itself.

## Brightness Control

**End-to-end resolution:** the full signal path from the pot's ADC read to the final PWM write is 12-bit throughout, with no intermediate rounding down to a coarser domain anywhere in between:

```
ADC read          "brightness"           gamma LUT              PWM duty
0-4095 (12-bit) -> 0-MAX_BRIGHTNESS -> (4096-entry table, -> 0-PWM_MAX_DUTY
(POT_ADC_MAX)      (config.h,           computed in float)     (12-bit,
                    = POT_ADC_MAX,                              PWM_RESOLUTION
                    = PWM_MAX_DUTY)                              in led_control.cpp)
```

`MAX_BRIGHTNESS`, `POT_ADC_MAX`, and `PWM_RESOLUTION`'s `PWM_MAX_DUTY` are all deliberately
set to the same value (4095) — `mapPotToBrightness()` (`pot_input.cpp`) turns into a pure
identity mapping (once the raw ADC reading is clamped to range), so the pot's full ADC
resolution passes through untouched. This used to be a real bottleneck: `brightness` was a
`uint8_t` (0-255) even though the ADC read 12-bit and gamma correction was computed in
float — every pot reading got crushed from 4096 possible positions down to only 256
distinguishable brightness levels *before* gamma or the PWM stage ever saw it, no matter how
much resolution either of those had downstream. Widening `brightness` itself (and everything
that carries it — the gamma LUT's index, the brightness slew target, the RTC-persisted
per-mode brightness in button mode, etc.) to match removes that bottleneck; only the actual
*mark on the dial* (which gesture, which gamma curve) matters now, not an accidental
resolution loss partway through.

**Gamma correction:** full LUT from 0–`MAX_BRIGHTNESS`, 4096 entries, gamma = 2.2 —
`calculateGammaLUT()` computes each entry with `pow()` in float for precision, then caches
the rounded result so `getCompensatedPWM()` doesn't call `pow()` on every PWM update. LUT
entry [0] = 0 for OFF transitions. `MIN_BRIGHTNESS_PWM` (config.h, in `MAX_BRIGHTNESS`-
equivalent units) is scaled internally to `MIN_PWM_DUTY` so the "never fully off while ON"
floor stays a consistent ~0.4% duty cycle even if `PWM_RESOLUTION` and `MAX_BRIGHTNESS` are
ever changed independently (today they're numerically equal, so the scaling is a no-op).

**Button-mode step size:** `incrementBrightness()`'s discrete step (button-mode long-press
dimming) is `BRIGHTNESS_STEP_SIZE` units per `BRIGHTNESS_STEP_MS` tick, not a literal 1 —
scaled up by the same factor `MAX_BRIGHTNESS` grew by (255 -> 4095) so a full-range
dim/brighten sweep still takes the same real-world time as before the resolution change.
Only the pot's *live* tracking needed the full domain; button mode's discrete stepping just
needed to not get 16x slower by accident.

**Continuous dimming (button mode):** hold → increment/decrement every `BRIGHTNESS_STEP_MS` (30 ms). Direction reverses on release. Double-flash (non-blocking) on boundary hit.

**Continuous dimming (pot mode):** no stepping or direction state — brightness eases toward the live pot reading via `updateBrightnessSlew()`, see above.

**Mode crossfade:** 400 ms linear interpolation between gamma-corrected PWM values. Both LEDs updated simultaneously. Implemented as a non-blocking state machine in `updateModeTransition()`.

**Boundary flash:** was blocking (`delay(80)` × 3 = 240 ms, dropped touch events). Now non-blocking state machine — `triggerBoundaryFlash()` sets state; `updateModeTransition()` advances it. PWM writes in `incrementBrightness()` are skipped while `boundaryFlashActive`.

## Battery Management

**Voltage divider:** 100 kΩ + 33 kΩ (ratio 4.030, ~11 µA drain @ 3.7 V).

**ADC:** 12-bit, 11 dB attenuation, `ADC_SAMPLE_COUNT` (8) samples averaged. Calibrated via `ADC_CALIBRATION_FACTOR`; `BMS_VOLTAGE_DROP` compensates for TP4056 MOSFET.

**State machine with hysteresis:**

| State | Voltage | Behaviour |
|-------|---------|-----------|
| NORMAL | > 3.5 V | Full operation |
| LOW | 3.2–3.5 V | Warning pulse on wake/turn-on, brightness capped at `LOW_MAX_BRIGHTNESS` (~50%), recurring every `BATTERY_INDICATOR_REPEAT_LOW_MS` (20 min) while ON |
| CRITICAL | 3.0–3.2 V | Warning pulse on every turn-on, brightness capped at `CRITICAL_MAX_BRIGHTNESS` (~25%), recurring every `BATTERY_INDICATOR_REPEAT_CRITICAL_MS` (5 min) while ON |
| CUTOFF | < 3.0 V | Refuse to turn on, enter deep sleep |

Hysteresis: LOW→CRITICAL requires 3 consecutive readings (90 s); CRITICAL→LOW needs > 3.3 V; CUTOFF→CRITICAL needs > 3.2 V (typically charging).

**Brightness ceiling (LOW/CRITICAL), both modes:** `getBatteryLimitedMaxBrightness()`
(`battery_monitor.cpp`) returns `LOW_MAX_BRIGHTNESS` / `CRITICAL_MAX_BRIGHTNESS` /
`MAX_BRIGHTNESS` for the current state; `led_control.cpp`'s `clampToBatteryLimit()` is the
single point every brightness-setting call site (`turnOn()`, `swapMode()`,
`updateBrightnessSlew()`) funnels a requested brightness through before it's stored in the
`brightness` variable that everything downstream (PWM, the battery indicator's own
brightness, boundary flash) reads from — so the ceiling applies uniformly no matter which
input mode or code path requested it. This used to only be enforced inside button mode's
`incrementBrightness()` (and only for CRITICAL, not LOW) — pot mode's live tracking never
went through it at all, so turning the pot to full still requested full brightness
regardless of battery state.

In pot mode this reads as a mechanical stop: `updateBrightnessSlew()` clamps the *value it
applies* every tick, but lets `brightnessSlewCurrent` keep easing toward wherever the pot
raw-points, uncapped, internally. Turning the dial past the position that would request more
than the ceiling just has no further visible effect; turning back down, the still-continuous
internal eased value crosses back under the cap and live tracking resumes with no jump or
extra lag. In button mode, `incrementBrightness()` has its own equivalent inline clamp
(rather than calling `clampToBatteryLimit()`) since it also needs to know *whether* a held
long-press just hit the ceiling, to trigger the same boundary-flash feedback as hitting the
true top of the range.

**Battery indicator pulse:** non-blocking sine-envelope animation. Sharpness encodes urgency (1.0 = smooth sine, 5.0 = sharp spike). In button mode, the button's own gesture recognition is blocked while playing; pot tracking and the accelerometer gesture are never blocked in either mode.

**Recurring reminder while ON:** the indicator isn't just a one-shot at turn-on/wake — `main.cpp`'s `showBatteryIndicator()` wrapper is the single trigger point every call site goes through (turn-on, wake, on-demand triple tap in button mode, and the periodic check in `loop()`), tracking when it was last shown and for which state. Two things make it fire again while the lamp stays ON:
1. **The battery state gets worse** (e.g. LOW → CRITICAL) — fires immediately, regardless of the repeat timer, so a mid-session degradation is never silently missed.
2. **The repeat interval elapses** while still LOW or CRITICAL — `BATTERY_INDICATOR_REPEAT_LOW_MS` / `BATTERY_INDICATOR_REPEAT_CRITICAL_MS` (config.h), CRITICAL repeating more often than LOW. Combined with the pulse shape itself already being sharper/faster at CRITICAL, this makes the reminder more insistent the lower the battery gets on two independent axes (how it looks, and how often it repeats).

The "worsened" memory resets once the battery recovers back to NORMAL, so a future dip announces immediately again rather than waiting out a stale interval.

## Power Management

**Deep sleep:** entered after `DEEP_SLEEP_TIMEOUT_MS` (60 s) in OFF state. GPIO10/GPIO5 must use `gpio_hold_en()` before sleep to prevent MOSFET leakage causing LED glow; `gpio_hold_dis()` on wake.

**Auto-off:** after `AUTO_OFF_TIMEOUT_MS` (4 h) with no user interaction while ON, `turnOff()` is called; deep sleep timer then starts. `lastInteractionTime` is updated in every gesture callback and on wake from deep sleep.

**Estimated power (component-datasheet math — not yet measured on real hardware; see `analysis/`):**

Base deep-sleep budget, both modes: ~10 µA (ESP32-C3) + 11 µA (battery divider) ≈ 21 µA.
Add ~6 µA if `USE_ACCEL_INPUT` is enabled (LIS3DH stays in low-power ODR mode — required
for wake in pot mode, optional for the mode-swap gesture in button mode) → ~27 µA.

- **Button mode:** no additional standby draw — a button has none. ~21–27 µA total →
  roughly 10.5–13.5 years theoretical on a 2500 mAh cell (12.5–16 years at 3000 mAh).
- **Pot mode:** a 10kΩ pot left wired straight across 3.3V/GND would draw
  V/R = 3.3V/10kΩ ≈ 330 µA continuously — deep sleep included, over 10x the rest of the
  standby budget combined. `POT_POWER_PIN` (see the Hardware section above) avoids this
  entirely by switching the pot's supply off during sleep, so pot mode's standby draw is
  the same ~21–27 µA base budget as button mode (LIS3DH is required here, so realistically
  ~27 µA — same decade-plus theoretical figure). This is why the pot is kept at 10kΩ rather
  than traded for a higher-resistance part: with the switch, standby power is solved
  without giving up any ADC accuracy.
  - This was previously (incorrectly) analyzed as an unavoidable ~330 µA standby cost with
    "negligible" framing that undersold it, and briefly considered trading pot resistance
    for lower power before landing on the switch instead — worth knowing if you see older
    numbers referenced elsewhere (e.g. old PR/commit history).
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

Survives deep sleep; lost on battery disconnect. Defaults (WARM, full brightness) apply after disconnect.

```cpp
RTC_DATA_ATTR uint8_t  savedMode      = MODE_WARM;
RTC_DATA_ATTR uint16_t warmBrightness = DEFAULT_BRIGHTNESS;  // = MAX_BRIGHTNESS
RTC_DATA_ATTR uint16_t coolBrightness = DEFAULT_BRIGHTNESS;
RTC_DATA_ATTR uint16_t bootCount      = 0;  // debug
```

`warmBrightness`/`coolBrightness` are only meaningful in button mode, where brightness is
an app-managed value that needs remembering per mode. In pot mode brightness is never
persisted — it's always just read fresh from the dial — so these two variables are declared
but unused there; `savedMode` is the only piece of state pot mode actually needs restored.

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
| Touch gestures (button mode) | `test/test_touch/test_touch_input.cpp` | 11 |
| Pot mapping, hysteresis & filtering (pot mode) | `test/test_pot/test_pot_input.cpp` | 16 |
| Battery state machine | `test/test_battery/test_battery_state_machine.cpp` | 15 |
