#ifndef CONFIG_H
#define CONFIG_H

// Debug output control
// Set to 1 to enable debug prints, 0 to disable
#define DEBUG 0

#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// Pin definitions
#define TOUCH_PIN 3
#define WHITE_LED_PIN 10    // White LED control (PWM)
#define WARM_LED_PIN 5      // Warm LED control (PWM) - GPIO5 is safe (GPIO9 is strapping pin)
#define BATTERY_PIN 0       // Battery voltage monitoring (ADC1_CH0)

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

// Gesture detection
#define GESTURE_WINDOW_MS      300   // Max time between taps in a multi-tap sequence

// LED mode identifiers
#define MODE_WARM 0
#define MODE_COOL 1

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

#endif // CONFIG_H
