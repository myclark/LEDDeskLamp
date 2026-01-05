#include "led_control.h"

// Brightness state
LampState currentLampState = OFF;
uint8_t brightnessLevel = BRIGHTNESS_STEPS;  // Start at max brightness
uint8_t brightnessPWM[BRIGHTNESS_STEPS];     // Gamma-corrected PWM values
int8_t brightnessDirection = -1;             // -1 = dimming, +1 = brightening

void initLED() {
  pinMode(WHITE_LED_PIN, OUTPUT);
  pinMode(WARM_LED_PIN, OUTPUT);

  // Calculate gamma-corrected brightness curve
  calculateGammaCurve();

  // Initialize LEDs to OFF state
  updateLED();

  DEBUG_PRINTLN("LED control initialized");
  DEBUG_PRINT("Brightness steps: ");
  DEBUG_PRINTLN(BRIGHTNESS_STEPS);
  DEBUG_PRINT("Gamma: ");
  DEBUG_PRINTLN(GAMMA_CORRECTION);
}

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
      DEBUG_PRINTLN("State: OFF");
      break;

    case WHITE:
      analogWrite(WHITE_LED_PIN, pwmValue);
      analogWrite(WARM_LED_PIN, 0);
      DEBUG_PRINT("State: WHITE, Brightness: ");
      DEBUG_PRINT(brightnessLevel);
      DEBUG_PRINT("/");
      DEBUG_PRINT(BRIGHTNESS_STEPS);
      DEBUG_PRINT(" (PWM: ");
      DEBUG_PRINT(pwmValue);
      DEBUG_PRINTLN(")");
      break;

    case WARM:
      analogWrite(WHITE_LED_PIN, 0);
      analogWrite(WARM_LED_PIN, pwmValue);
      DEBUG_PRINT("State: WARM, Brightness: ");
      DEBUG_PRINT(brightnessLevel);
      DEBUG_PRINT("/");
      DEBUG_PRINT(BRIGHTNESS_STEPS);
      DEBUG_PRINT(" (PWM: ");
      DEBUG_PRINT(pwmValue);
      DEBUG_PRINTLN(")");
      break;
  }
}

void stepBrightness() {
  DEBUG_PRINTLN(">>> BRIGHTNESS STEP");

  // Only adjust brightness when in WHITE or WARM mode
  if (currentLampState == WHITE || currentLampState == WARM) {
    // Adjust brightness in current direction
    brightnessLevel += brightnessDirection;

    // Reverse direction at boundaries
    if (brightnessLevel >= BRIGHTNESS_STEPS) {
      brightnessLevel = BRIGHTNESS_STEPS;
      brightnessDirection = -1;  // Start dimming
      DEBUG_PRINTLN("(Reached MAX, reversing to dim)");
    } else if (brightnessLevel <= 1) {
      brightnessLevel = 1;
      brightnessDirection = 1;   // Start brightening
      DEBUG_PRINTLN("(Reached MIN, reversing to brighten)");
    }

    updateLED();
  }
}

void calculateGammaCurve() {
  for (uint8_t i = 0; i < BRIGHTNESS_STEPS; i++) {
    // Calculate linear brightness (0.0 to 1.0)
    float linearBrightness = (float)(i + 1) / (float)BRIGHTNESS_STEPS;

    // Apply gamma correction
    float corrected = pow(linearBrightness, GAMMA_CORRECTION);

    // Scale to PWM range (0 to MAX_BRIGHTNESS)
    brightnessPWM[i] = (uint8_t)(corrected * MAX_BRIGHTNESS);

    // Debug output
    DEBUG_PRINT("Level ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINT(": PWM = ");
    DEBUG_PRINTLN(brightnessPWM[i]);
  }
}
