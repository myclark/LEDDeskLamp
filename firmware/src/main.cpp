#include <Arduino.h>

#define TOUCH_PIN 3
#define WHITE_LED_PIN 10   // White LED control (PWM)
#define WARM_LED_PIN 9   // Warm LED control (PWM)

// Maximum brightness for testing (0-255)
// Set to 128 (50%) for 3.3V testing, increase to 255 for full battery voltage
#define MAX_BRIGHTNESS 128

// Brightness configuration
#define BRIGHTNESS_STEPS 8      // Number of brightness levels
#define GAMMA_CORRECTION 2.2    // Gamma curve for perceptual brightness (2.0-2.5 typical)

// Timing thresholds (milliseconds)
const unsigned long DEBOUNCE_MS = 50;
const unsigned long LONG_PRESS_MS = 800;
const unsigned long BRIGHTNESS_STEP_MS = 400;  // Time between brightness steps when holding

// State machine
enum LampState {
  OFF,
  WHITE,
  WARM
};

LampState currentLampState = OFF;

// Brightness state
uint8_t brightnessLevel = BRIGHTNESS_STEPS;  // Start at max brightness
uint8_t brightnessPWM[BRIGHTNESS_STEPS];     // Gamma-corrected PWM values
int8_t brightnessDirection = -1;             // -1 = dimming, +1 = brightening

// Button state
bool lastState = false;
bool currentState = false;
unsigned long pressStartTime = 0;
bool longPressFired = false;
unsigned long lastBrightnessChange = 0;
bool brightnessStepMode = false;

// Callback function pointers
void (*onTap)() = nullptr;
void (*onLongPress)() = nullptr;

// Forward declarations
void updateButton();
void handleTap();
void handleLongPress();
void advanceState();
void updateLED();
void calculateGammaCurve();

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);
  pinMode(WARM_LED_PIN, OUTPUT);

  // Calculate gamma-corrected brightness curve
  calculateGammaCurve();

  // Attach your callbacks
  onTap = handleTap;
  onLongPress = handleLongPress;

  // Initialize LEDs to OFF state
  updateLED();

  Serial.println("Lamp ready");
  Serial.println("Tap: Cycle WHITE -> WARM -> OFF");
  Serial.println("Hold: Step through brightness levels (when ON)");
  Serial.print("Brightness steps: ");
  Serial.print(BRIGHTNESS_STEPS);
  Serial.print(", Gamma: ");
  Serial.println(GAMMA_CORRECTION);
}

void loop() {
  updateButton();

  // Your other code here...
  delay(10);  // Small delay to avoid hammering the CPU
}

void updateButton() {
  bool reading = digitalRead(TOUCH_PIN) == HIGH;

  // Debounce: only accept state change if stable
  static unsigned long lastChangeTime = 0;

  if (reading != lastState) {
    lastChangeTime = millis();
  }

  if ((millis() - lastChangeTime) > DEBOUNCE_MS) {
    // State has been stable long enough

    if (reading != currentState) {
      currentState = reading;

      if (currentState) {
        // Button just pressed
        pressStartTime = millis();
        longPressFired = false;
        brightnessStepMode = false;
        Serial.println("Press started");
      } else {
        // Button just released
        if (!longPressFired) {
          // It was a tap (released before long press threshold)
          if (onTap) onTap();
        }
        brightnessStepMode = false;
        Serial.println("Released");
      }
    }

    // Check for long press while held
    if (currentState && !longPressFired) {
      if ((millis() - pressStartTime) >= LONG_PRESS_MS) {
        longPressFired = true;
        brightnessStepMode = true;
        lastBrightnessChange = millis();
        if (onLongPress) onLongPress();
      }
    }

    // Continue stepping brightness while held
    if (currentState && brightnessStepMode) {
      if ((millis() - lastBrightnessChange) >= BRIGHTNESS_STEP_MS) {
        lastBrightnessChange = millis();
        if (onLongPress) onLongPress();
      }
    }
  }

  lastState = reading;
}

// === State machine functions ===

void advanceState() {
  switch (currentLampState) {
    case OFF:
      currentLampState = WHITE;
      break;
    case WHITE:
      currentLampState = WARM;
      break;
    case WARM:
      currentLampState = OFF;
      break;
  }
}

void updateLED() {
  uint8_t pwmValue = brightnessPWM[brightnessLevel - 1];

  switch (currentLampState) {
    case OFF:
      analogWrite(WHITE_LED_PIN, 0);
      analogWrite(WARM_LED_PIN, 0);
      Serial.println("State: OFF");
      break;

    case WHITE:
      analogWrite(WHITE_LED_PIN, pwmValue);
      analogWrite(WARM_LED_PIN, 0);
      Serial.print("State: WHITE, Brightness: ");
      Serial.print(brightnessLevel);
      Serial.print("/");
      Serial.print(BRIGHTNESS_STEPS);
      Serial.print(" (PWM: ");
      Serial.print(pwmValue);
      Serial.println(")");
      break;

    case WARM:
      analogWrite(WHITE_LED_PIN, 0);
      analogWrite(WARM_LED_PIN, pwmValue);
      Serial.print("State: WARM, Brightness: ");
      Serial.print(brightnessLevel);
      Serial.print("/");
      Serial.print(BRIGHTNESS_STEPS);
      Serial.print(" (PWM: ");
      Serial.print(pwmValue);
      Serial.println(")");
      break;
  }
}

// === Callback implementations ===

void handleTap() {
  Serial.println(">>> TAP detected!");
  advanceState();
  updateLED();
}

void handleLongPress() {
  Serial.println(">>> BRIGHTNESS STEP");

  // Only adjust brightness when in WHITE or WARM mode
  if (currentLampState == WHITE || currentLampState == WARM) {
    // Adjust brightness in current direction
    brightnessLevel += brightnessDirection;

    // Reverse direction at boundaries
    if (brightnessLevel >= BRIGHTNESS_STEPS) {
      brightnessLevel = BRIGHTNESS_STEPS;
      brightnessDirection = -1;  // Start dimming
      Serial.println("(Reached MAX, reversing to dim)");
    } else if (brightnessLevel <= 1) {
      brightnessLevel = 1;
      brightnessDirection = 1;   // Start brightening
      Serial.println("(Reached MIN, reversing to brighten)");
    }

    updateLED();
  }
  // Do nothing if in OFF state
}

// Calculate gamma-corrected brightness curve
void calculateGammaCurve() {
  for (uint8_t i = 0; i < BRIGHTNESS_STEPS; i++) {
    // Calculate linear brightness (0.0 to 1.0)
    float linearBrightness = (float)(i + 1) / (float)BRIGHTNESS_STEPS;

    // Apply gamma correction
    float corrected = pow(linearBrightness, GAMMA_CORRECTION);

    // Scale to PWM range (0 to MAX_BRIGHTNESS)
    brightnessPWM[i] = (uint8_t)(corrected * MAX_BRIGHTNESS);

    // Debug output
    Serial.print("Level ");
    Serial.print(i + 1);
    Serial.print(": PWM = ");
    Serial.println(brightnessPWM[i]);
  }
}
