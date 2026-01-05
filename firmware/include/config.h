#ifndef CONFIG_H
#define CONFIG_H

// Debug output control
// Set to 1 to enable debug prints, 0 to disable
#define DEBUG 1

#if DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// Pin definitions
#define TOUCH_PIN 3
#define WHITE_LED_PIN 10   // White LED control (PWM)
#define WARM_LED_PIN 9     // Warm LED control (PWM)

// Maximum brightness for testing (0-255)
// Set to 128 (50%) for 3.3V testing, increase to 255 for full battery voltage
#define MAX_BRIGHTNESS 128

// Brightness configuration
#define BRIGHTNESS_STEPS 8      // Number of brightness levels
#define GAMMA_CORRECTION 2.2    // Gamma curve for perceptual brightness (2.0-2.5 typical)

// Timing thresholds (milliseconds)
#define DEBOUNCE_MS 50
#define LONG_PRESS_MS 800
#define BRIGHTNESS_STEP_MS 400  // Time between brightness steps when holding

#endif // CONFIG_H
