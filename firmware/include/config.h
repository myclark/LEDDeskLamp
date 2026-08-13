#ifndef CONFIG_H
#define CONFIG_H

// Debug output control
// Set to 1 to enable debug prints, 0 to disable
#define DEBUG 0

#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINT2(x, fmt) Serial.print(x, fmt)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTLN2(x, fmt) Serial.println(x, fmt)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINT2(x, fmt)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTLN2(x, fmt)
#endif

// Pin definitions
// BUTTON_PIN and POT_PIN are the same physical GPIO — only one is actually wired up,
// matching whichever primary input mode is selected below (see USE_POT_INPUT).
//
// BUTTON_PIN: physical momentary button. Wire the button between BUTTON_PIN and 3.3V,
// with an external ~10kΩ pull-down resistor from BUTTON_PIN to GND. Idle = LOW, pressed = HIGH.
// (Same active-HIGH polarity as the old TTP223 module, so touch_input.cpp needs no changes.)
#define BUTTON_PIN 3         // Physical button input; also the deep-sleep wake pin (button mode)
//
// POT_PIN: potentiometer wiper. Wire one outer leg to POT_POWER_PIN (below, NOT the fixed
// 3.3V rail) and the other to GND; wiper to POT_PIN (ADC1-capable). Fully counter-clockwise
// = OFF, fully clockwise = MAX_BRIGHTNESS.
//
// Recommended part + RC front end (analog complement to the digital filtering in
// pot_input.cpp — see POT_FILTER_TIME_CONSTANT_MS below):
//   - 10kΩ LINEAR taper pot (not audio/log — mapPotToBrightness() is a straight linear
//     map, so a log-taper pot would make brightness feel badly non-uniform across travel).
//     10kΩ is chosen for best ADC accuracy (low source impedance) — its standby current is
//     no longer a tradeoff now that POT_POWER_PIN switches it off during deep sleep, so
//     there's no reason to trade accuracy for a higher-value pot.
//   - Wiper -> 1kΩ series resistor -> POT_PIN, then a 1µF ceramic cap from POT_PIN to GND.
//     ~160Hz cutoff: ~30x faster than the digital filter (doesn't add felt lag) and ~30x
//     below the 5kHz LED PWM frequency (knocks that noise source down significantly).
//     Keeps total source impedance (pot's own ~2.5kΩ worst case + this 1kΩ) comfortably
//     under the ESP32 ADC's recommended limit for accurate 12-bit reads.
#define POT_PIN 3            // Potentiometer wiper input (pot mode)
//
// POT_POWER_PIN: powers the pot's divider directly from a GPIO instead of the fixed 3.3V
// rail, so it can be switched off during deep sleep — without it, a 10kΩ pot left wired
// across 3.3V/GND draws ~330µA continuously (deep sleep included), over 10x the rest of
// the standby budget. No external switch transistor is needed: at the pot's ~330µA draw,
// the GPIO driver's on-resistance drop is a few mV at most, well within ESP32-C3's GPIO
// sourcing capability (tens of mA) — driving the pin HIGH is electrically indistinguishable
// from tying it to the 3.3V rail at this current level. Must be an RTC-capable pin (0-5 on
// ESP32-C3) so gpio_hold_en()/gpio_hold_dis() can latch it LOW through sleep, same pattern
// already used for the LED pins in enterDeepSleep(). GPIO1 is the only one of 0-5 not
// otherwise spoken for (0=battery ADC, 2=strapping/avoid, 3=POT_PIN, 4=LIS3DH INT, 5=warm LED).
#define POT_POWER_PIN 1      // Switched power for the pot divider (pot mode only)
// Settle time after powering the pot back on (wake or cold boot) before the first reading
// is trusted — must clear the RC front end's worst-case settling time (~5x its ~3.5ms time
// constant ≈ 17.5ms); comfortable margin above that.
#define POT_SETTLE_MS 25
#define WHITE_LED_PIN 10    // White LED control (PWM)
#define WARM_LED_PIN 5      // Warm LED control (PWM) - GPIO5 is safe (GPIO9 is strapping pin)
#define BATTERY_PIN 0       // Battery voltage monitoring (ADC1_CH0)
#define LIS3DH_SDA_PIN 8    // LIS3DH I2C data
#define LIS3DH_SCL_PIN 9    // LIS3DH I2C clock (GPIO9 is a strapping pin, but open-drain I2C is safe after reset)
#define LIS3DH_INT_PIN 4    // LIS3DH INT1 — separate pin from BUTTON_PIN/POT_PIN which own GPIO3

// Battery voltage calibration
#define ADC_CALIBRATION_FACTOR 0.904  // Tuned to oscilloscope reading (5.246V actual → 5.63V calculated)
#define BMS_VOLTAGE_DROP 0.090        // TP4056 MOSFET voltage drop (~90mV)

// Maximum brightness for testing (0-255)
// Set to 128 (50%) for 3.3V testing, increase to 255 for full battery voltage
#define MAX_BRIGHTNESS 255
#define MIN_BRIGHTNESS_PWM 1         // Minimum PWM output value (prevents LED completely off)

// Brightness configuration
#define GAMMA_CORRECTION 2.2        // Gamma curve for perceptual brightness (2.0-2.5 typical)
#define BRIGHTNESS_STEP_MS 30       // Time between brightness increments when holding (continuous)
#define MODE_TRANSITION_MS 400      // Smooth fade duration when changing modes
// Exponential smoothing time constant for live brightness tracking (potentiometer mode).
// Larger = slower, dreamier follow; smaller = snappier/more direct. At this time constant,
// brightness reaches ~95% of a new target after roughly 3x this value in ms.
#define BRIGHTNESS_SLEW_TIME_CONSTANT_MS 150

// Timing thresholds (milliseconds)
#define DEBOUNCE_MS 50
#define LONG_PRESS_MS 800
#define DEEP_SLEEP_TIMEOUT_MS 60000     // Enter deep sleep after 1 minute in OFF state
#define AUTO_OFF_ENABLED 1              // Set to 0 to disable auto-off
#define AUTO_OFF_TIMEOUT_MS 14400000    // Auto-off after 4 hours with no interaction
#define USB_CDC_INIT_DELAY_MS 100       // Delay for USB CDC enumeration on boot

// Watchdog: if the main loop doesn't check in within this window (I2C bus lockup,
// a future bug, etc.), the task watchdog reboots the device instead of staying frozen.
#define WATCHDOG_TIMEOUT_MS 8000
// Bounds every Wire (I2C) transaction so a bus glitch on the accelerometer link can't
// block loop() indefinitely — it fails fast instead and the watchdog above is just the backstop.
#define I2C_TIMEOUT_MS 50

// Gesture detection
#define GESTURE_WINDOW_MS 300   // Max time between taps in a multi-tap sequence

// Boundary flash (double-flash feedback when brightness hits min/max)
#define BOUNDARY_FLASH_STEP_MS 80   // Duration of each on/off step in the double flash

// LED mode identifiers
#define MODE_WARM 0
#define MODE_COOL 1

// ADC sampling
#define ADC_SAMPLE_COUNT 8           // Number of ADC samples to average for battery voltage

// ── Potentiometer tuning (only relevant when USE_POT_INPUT is defined) ─────────
#define POT_ADC_MAX 4095             // Full-scale ADC reading at 12-bit resolution
#define POT_SAMPLE_COUNT 4           // ADC samples averaged per read (light denoise, no delay needed)
// On/off hysteresis, in mapped brightness units (0..MAX_BRIGHTNESS), not raw ADC counts.
// Two different thresholds prevent flicker right at the boundary: once ON, the pot must
// drop to/below POT_OFF_THRESHOLD to turn off; once OFF, it must rise to/above the higher
// POT_ON_HYSTERESIS to turn back on. Between the two, the lamp just holds its last state.
#define POT_OFF_THRESHOLD 8          // ~3% of full scale
#define POT_ON_HYSTERESIS 13         // ~5% of full scale
// Top-end dead zone, mirroring the bottom: pots rarely hit their mechanical/electrical
// limit exactly, so without this the user could never quite reach 100% by feel. Once the
// mapped brightness is within this many units of MAX_BRIGHTNESS, it snaps to exactly
// MAX_BRIGHTNESS. This is a plain value clamp (unlike the bottom, which needs a full
// on/off hysteresis state machine since crossing it is a functional state change).
#define POT_MAX_DEADZONE 8           // ~3% of full scale, same margin as POT_OFF_THRESHOLD
// Minimum pot movement (brightness units) that counts as user interaction for the
// auto-off timer — filters out ADC jitter that would otherwise reset it forever.
#define POT_MOVEMENT_DEADBAND 2
// General noise filtering across the whole travel (not just the two ends): exponential
// smoothing applied to the pot's reading, tick to tick, before it's used for anything —
// the on/off decision, the brightness target, all of it. Distinct from
// BRIGHTNESS_SLEW_TIME_CONSTANT_MS, which shapes the visible ramp of the *output* PWM;
// this smooths the *input* signal so ADC noise can't cause spurious on/off toggling or
// target jitter at any dial position, not just near the endpoints.
#define POT_FILTER_TIME_CONSTANT_MS 30

// Battery voltage thresholds (volts)
#define BATTERY_FULL 4.2
#define BATTERY_NOMINAL 3.7
#define BATTERY_LOW_THRESHOLD 3.5
#define BATTERY_CRITICAL_THRESHOLD 3.2
#define BATTERY_CRITICAL_HYSTERESIS 3.3   // Must rise above this to return from CRITICAL to LOW
#define BATTERY_CUTOFF_THRESHOLD 3.0
#define BATTERY_CUTOFF_RECOVERY_HYSTERESIS 3.2  // Must rise above this to recover from CUTOFF

// Battery monitoring timing
#define BATTERY_READ_INTERVAL_MS 30000    // Read every 30 seconds (30s × 3 = 90s for critical)
#define BATTERY_DISPLAY_INTERVAL_MS 60000 // Auto-display every 60 seconds

// Battery state machine hysteresis
#define CRITICAL_CONSECUTIVE_THRESHOLD 3  // Consecutive low readings before entering CRITICAL

// Battery brightness limiting
#define CRITICAL_MAX_BRIGHTNESS 64        // Max brightness in CRITICAL state (~25% of 255)

// Voltage divider ratio: (100kΩ + 33kΩ) / 33kΩ = 4.030
#define VOLTAGE_DIVIDER_RATIO 4.030

// Brightness compensation reference: at this voltage PWM is used at full value
#define BRIGHTNESS_REFERENCE_VOLTAGE 3.5

// Programming mode: when voltage > this, we're on USB (not battery)
#define PROGRAMMING_MODE_VOLTAGE 4.5
#define PROGRAMMING_MODE_MAX_PWM 128  // Cap at 50% when on USB power

// Battery indicator pulse configuration
#define PULSE_MIN_BRIGHTNESS 64      // Minimum brightness for pulse indicator (~25% of 255)

// Pulse parameters per battery state: count, period (ms), sharpness (1.0=sine, higher=sharper)
#define PULSE_NORMAL_COUNT 3
#define PULSE_NORMAL_PERIOD_MS 800
#define PULSE_NORMAL_SHARPNESS 1.0

#define PULSE_LOW_COUNT 2
#define PULSE_LOW_PERIOD_MS 500
#define PULSE_LOW_SHARPNESS 2.0

#define PULSE_CRITICAL_COUNT 1
#define PULSE_CRITICAL_PERIOD_MS 300
#define PULSE_CRITICAL_SHARPNESS 5.0

// Recurring low-battery reminder while ON: the indicator plays immediately whenever the
// battery state worsens into LOW/CRITICAL (whether that happens at turn-on/wake, or while
// already ON and draining), then repeats periodically as a continuing reminder. CRITICAL
// repeats more often than LOW — more insistent the lower the battery gets, on top of the
// pulse shape itself already being sharper/faster at CRITICAL (see PULSE_* above).
#define BATTERY_INDICATOR_REPEAT_LOW_MS      (20UL * 60 * 1000)  // 20 min
#define BATTERY_INDICATOR_REPEAT_CRITICAL_MS (5UL * 60 * 1000)   // 5 min

// Default brightness on first power-up (RTC memory cleared)
#define DEFAULT_BRIGHTNESS 255

// ── Input mode selection ───────────────────────────────────────────────────
// Primary control: choose exactly one physical input for on/off + brightness.
//
// Define USE_POT_INPUT for a potentiometer (pot_input.cpp): turning it fully
// counter-clockwise requests OFF, anywhere above that requests ON at a brightness
// proportional to position, eased with BRIGHTNESS_SLEW_TIME_CONSTANT_MS. Brightness is
// always just "wherever the pot is right now" — there's nothing to restore from RTC memory.
// Comment out to use the physical momentary button instead (touch_input.cpp gesture
// engine on BUTTON_PIN): single tap on/off, long press brightness, triple tap battery
// indicator.
#define USE_POT_INPUT

// Define USE_ACCEL_INPUT to enable the LIS3DH accelerometer as an auxiliary trigger for
// the mode-swap gesture (a tap on the lamp body swaps WARM/COOL). In button mode this is
// optional — double-tapping the button also swaps modes. In pot mode this is required:
// the accelerometer is the only deep-sleep wake source once there's no button, since the
// ESP32-C3 can't wake from an ADC threshold.
#define USE_ACCEL_INPUT

#if defined(USE_POT_INPUT) && !defined(USE_ACCEL_INPUT)
#error "USE_POT_INPUT requires USE_ACCEL_INPUT: with no button, the accelerometer tap is the only way to wake the device from deep sleep."
#endif

// ── LIS3DH tap-detection accelerometer ────────────────────────────────────
// Only relevant when USE_ACCEL_INPUT is defined.
#define LIS3DH_I2C_ADDR      0x19   // Default; 0x18 if address jumper bridged

#define LIS3DH_CLICK_CFG     0x15   // Single-tap only, all axes (ZS+YS+XS). Hardware double-
                                    // tap discrimination is still disabled (ring-down from a
                                    // physical tap falls within the hardware double-tap
                                    // window, making every tap look like a Dclick) — the
                                    // double/triple-tap gesture below is counted in firmware
                                    // from single-tap (Sclick) events instead.
#define LIS3DH_CLICK_THS     0x20   // ~512 mg threshold
#define LIS3DH_CTRL_REG1     0x57   // 100 Hz low-power, X+Y+Z enabled (~6 µA)
#define LIS3DH_TIME_LIMIT    0x06   // 60 ms max tap impulse window — filters slow movement transients
#define LIS3DH_TIME_LATENCY  0x10   // 160 ms dead time after first tap (unused: hardware
                                    // double-tap detection is disabled, see LIS3DH_CLICK_CFG)
#define LIS3DH_TIME_WINDOW   0x18   // 240 ms second-tap acceptance window (unused, same reason)
// Firmware-side multi-tap gesture (see updateAccelInput() in main.cpp): a single tap is
// always an incidental bump and ignored (turning the pot knob shakes the enclosure the
// accelerometer is mounted to). Mode swap fires on an *exact* tap count, not "2 or more" —
// this leaves the other of {double, triple} tap free for a future gesture without needing
// new hardware. Must be 2 or 3: 1 is reserved as the incidental-bump filter, and higher
// counts get progressively harder for a user to land deliberately.
#define ACCEL_MODE_SWAP_TAP_COUNT 2   // 2 = double tap swaps mode, triple tap is unused/free
                                      // 3 = triple tap swaps mode, double tap is unused/free
#if ACCEL_MODE_SWAP_TAP_COUNT < 2 || ACCEL_MODE_SWAP_TAP_COUNT > 3
#error "ACCEL_MODE_SWAP_TAP_COUNT must be 2 or 3 — a single tap must stay reserved as the incidental-bump filter (see doc/accel_input_integration.md)."
#endif
// Dead time after each detected tap to absorb that tap's own ring-down before watching for
// the next one in the sequence.
#define LIS3DH_RING_SUPPRESS_MS   300
// Window after ring-down closes to catch the next tap; if it passes without exactly
// ACCEL_MODE_SWAP_TAP_COUNT taps counted, the gesture is discarded rather than swapping
// the mode — including if the count overshoots (e.g. a stray extra tap during a double).
#define LIS3DH_GESTURE_WINDOW_MS  250

#endif // CONFIG_H
