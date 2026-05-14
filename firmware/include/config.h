#ifndef CONFIG_H
#define CONFIG_H

// Debug output control
// Set to 1 to enable debug prints, 0 to disable
#define DEBUG 1

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
#define TOUCH_PIN 3         // Note this can also be reused as a interrupt pin
#define WHITE_LED_PIN 10    // White LED control (PWM)
#define WARM_LED_PIN 5      // Warm LED control (PWM) - GPIO5 is safe (GPIO9 is strapping pin)
#define BATTERY_PIN 0       // Battery voltage monitoring (ADC1_CH0)
#define LIS3DH_SDA_PIN 8    // LIS3DH I2C data
#define LIS3DH_SCL_PIN 9    // LIS3DH I2C clock (GPIO9 is a strapping pin, but open-drain I2C is safe after reset)
#define LIS3DH_INT_PIN 3    // LIS3DH INT1 — reuses same GPIO as TOUCH_PIN

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

// Timing thresholds (milliseconds)
#define DEBOUNCE_MS 50
#define LONG_PRESS_MS 800
#define DEEP_SLEEP_TIMEOUT_MS 60000     // Enter deep sleep after 1 minute in OFF state
#define AUTO_OFF_ENABLED 1              // Set to 0 to disable auto-off
#define AUTO_OFF_TIMEOUT_MS 14400000    // Auto-off after 4 hours with no interaction
#define USB_CDC_INIT_DELAY_MS 100       // Delay for USB CDC enumeration on boot

// Gesture detection
#define GESTURE_WINDOW_MS 300   // Max time between taps in a multi-tap sequence

// Boundary flash (double-flash feedback when brightness hits min/max)
#define BOUNDARY_FLASH_STEP_MS 80   // Duration of each on/off step in the double flash

// LED mode identifiers
#define MODE_WARM 0
#define MODE_COOL 1

// ADC sampling
#define ADC_SAMPLE_COUNT 8           // Number of ADC samples to average for battery voltage

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

// Default brightness on first power-up (RTC memory cleared)
#define DEFAULT_BRIGHTNESS 128

// ── Input mode selection ───────────────────────────────────────────────────
// Define USE_ACCEL_INPUT to use LIS3DH tap detection instead of TTP223.
// Comment out to revert to capacitive touch (touch_input gesture engine).
#define USE_ACCEL_INPUT

// ── LIS3DH tap-detection accelerometer ────────────────────────────────────
// Only relevant when USE_ACCEL_INPUT is defined.
#define LIS3DH_I2C_ADDR      0x19   // Default; 0x18 if address jumper bridged

#define LIS3DH_CLICK_CFG     0x15   // Single-tap only, all axes (ZS+YS+XS). Double-tap
                                    // discrimination is done in firmware, not hardware, because
                                    // ring-down from a physical tap falls within the hardware
                                    // double-tap window and makes every tap look like a Dclick.
#define LIS3DH_CLICK_THS     0x10   // ~256 mg threshold
#define LIS3DH_CTRL_REG1     0x57   // 100 Hz low-power, X+Y+Z enabled (~6 µA)
#define LIS3DH_TIME_LIMIT    0x0F   // 150 ms max tap impulse window (physical enclosures ring longer)
#define LIS3DH_TIME_LATENCY  0x10   // 160 ms dead time after first tap
#define LIS3DH_TIME_WINDOW   0x18   // 240 ms second-tap acceptance window
// After first Sclick, ignore INT1 for this long to suppress ring-down re-triggers,
// then open a window to watch for a deliberate second tap.
#define LIS3DH_RING_SUPPRESS_MS   300   // Dead time after first tap (covers ring-down)
#define LIS3DH_SECOND_TAP_MS      250   // Window after ring-down to catch second tap
// Post-dispatch cooldown before re-arming (prevents double-dispatch from same event)
#define LIS3DH_COOLDOWN_MS        300

#endif // CONFIG_H
