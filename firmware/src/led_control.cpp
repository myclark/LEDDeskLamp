#include "led_control.h"

// Forward declaration
static void flashBoundaryIndicator();

// Brightness state
LampState currentLampState = OFF;
uint8_t brightness = MAX_BRIGHTNESS;       // Continuous brightness (0 to MAX_BRIGHTNESS)
uint8_t gammaLUT[MAX_BRIGHTNESS + 1];      // Gamma-corrected lookup table
int8_t brightnessDirection = -1;           // -1 = dimming, +1 = brightening

// Smooth mode transition state (for OFF/WHITE/WARM changes)
float currentWhitePWM = 0.0;   // Current actual PWM (smooth)
float currentWarmPWM = 0.0;    // Current actual PWM (smooth)
float targetWhitePWM = 0.0;    // Target PWM
float targetWarmPWM = 0.0;     // Target PWM
float startWhitePWM = 0.0;     // PWM at transition start
float startWarmPWM = 0.0;      // PWM at transition start
unsigned long transitionStartTime = 0;
bool isTransitioning = false;

void initLED() {
  pinMode(WHITE_LED_PIN, OUTPUT);
  pinMode(WARM_LED_PIN, OUTPUT);

  // Calculate gamma-corrected lookup table
  calculateGammaLUT();

  // Initialize LEDs to OFF state
  updateLED();

  DEBUG_PRINTLN("LED control initialized");
  DEBUG_PRINT("Continuous brightness (0-");
  DEBUG_PRINT(MAX_BRIGHTNESS);
  DEBUG_PRINT("), Gamma: ");
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
  // Get gamma-corrected PWM value for current brightness
  uint8_t pwmValue = gammaLUT[brightness];

  // Set target PWM values for smooth mode transition
  switch (currentLampState) {
    case OFF:
      targetWhitePWM = 0;
      targetWarmPWM = 0;
      DEBUG_PRINTLN("State: OFF");
      break;

    case WHITE:
      targetWhitePWM = pwmValue;
      targetWarmPWM = 0;
      DEBUG_PRINT("State: WHITE, Brightness: ");
      DEBUG_PRINT(brightness);
      DEBUG_PRINT("/");
      DEBUG_PRINT(MAX_BRIGHTNESS);
      DEBUG_PRINT(" (PWM: ");
      DEBUG_PRINT(pwmValue);
      DEBUG_PRINTLN(")");
      break;

    case WARM:
      targetWhitePWM = 0;
      targetWarmPWM = pwmValue;
      DEBUG_PRINT("State: WARM, Brightness: ");
      DEBUG_PRINT(brightness);
      DEBUG_PRINT("/");
      DEBUG_PRINT(MAX_BRIGHTNESS);
      DEBUG_PRINT(" (PWM: ");
      DEBUG_PRINT(pwmValue);
      DEBUG_PRINTLN(")");
      break;
  }

  // Start smooth transition from current position
  startWhitePWM = currentWhitePWM;
  startWarmPWM = currentWarmPWM;
  transitionStartTime = millis();
  isTransitioning = true;
}

void incrementBrightness() {
  // Only adjust brightness when in WHITE or WARM mode
  if (currentLampState != WHITE && currentLampState != WARM) {
    return;  // Ignore brightness changes in OFF mode
  }

  uint8_t oldBrightness = brightness;

  // Continuously increment/decrement brightness
  int newBrightness = (int)brightness + brightnessDirection;

  // Clamp at boundaries (1 to MAX_BRIGHTNESS) and flash
  // Note: brightness=1 outputs MIN_BRIGHTNESS_PWM from LUT
  if (newBrightness >= MAX_BRIGHTNESS) {
    brightness = MAX_BRIGHTNESS;
    if (oldBrightness != MAX_BRIGHTNESS) {
      DEBUG_PRINTLN("(Reached MAX - release and hold again to dim)");
      flashBoundaryIndicator();
    }
  } else if (newBrightness <= 1) {
    brightness = 1;  // Minimum brightness (maps to MIN_BRIGHTNESS_PWM via LUT)
    if (oldBrightness != 1) {
      DEBUG_PRINTLN("(Reached MIN - release and hold again to brighten)");
      flashBoundaryIndicator();
    }
  } else {
    brightness = (uint8_t)newBrightness;
  }

  // Directly update PWM (no transition for continuous brightness changes)
  uint8_t pwmValue = gammaLUT[brightness];
  if (currentLampState == WHITE) {
    currentWhitePWM = pwmValue;
    targetWhitePWM = pwmValue;
    analogWrite(WHITE_LED_PIN, pwmValue);
  } else if (currentLampState == WARM) {
    currentWarmPWM = pwmValue;
    targetWarmPWM = pwmValue;
    analogWrite(WARM_LED_PIN, pwmValue);
  }
}

void flashBoundaryIndicator() {
  // Double flash to indicate min/max boundary (more deliberate than single flash)
  uint8_t activeLEDPin = (currentLampState == WHITE) ? WHITE_LED_PIN : WARM_LED_PIN;
  uint8_t currentPWM = gammaLUT[brightness];

  // First flash
  analogWrite(activeLEDPin, 0);
  delay(80);
  analogWrite(activeLEDPin, currentPWM);
  delay(80);

  // Second flash
  analogWrite(activeLEDPin, 0);
  delay(80);
  analogWrite(activeLEDPin, currentPWM);
}

void calculateGammaLUT() {
  // Generate gamma-corrected lookup table for full brightness range
  for (int i = 0; i <= MAX_BRIGHTNESS; i++) {
    // Calculate linear brightness (0.0 to 1.0)
    float linearBrightness = (float)i / (float)MAX_BRIGHTNESS;

    // Apply gamma correction
    float corrected = pow(linearBrightness, GAMMA_CORRECTION);

    // Scale to PWM range (0 to MAX_BRIGHTNESS)
    uint8_t pwmValue = (uint8_t)(corrected * MAX_BRIGHTNESS);

    // Enforce minimum PWM (except for index 0 which is true off)
    if (i > 0 && pwmValue < MIN_BRIGHTNESS_PWM) {
      pwmValue = MIN_BRIGHTNESS_PWM;
    }

    gammaLUT[i] = pwmValue;
  }

  DEBUG_PRINTLN("Gamma LUT generated");
  DEBUG_PRINT("Sample values - 0: ");
  DEBUG_PRINT(gammaLUT[0]);
  DEBUG_PRINT(", 1: ");
  DEBUG_PRINT(gammaLUT[1]);
  DEBUG_PRINT(", Mid: ");
  DEBUG_PRINT(gammaLUT[MAX_BRIGHTNESS / 2]);
  DEBUG_PRINT(", Max: ");
  DEBUG_PRINTLN(gammaLUT[MAX_BRIGHTNESS]);
}

void updateModeTransition() {
  if (!isTransitioning) {
    return;  // No transition in progress
  }

  // Calculate transition progress (0.0 to 1.0)
  unsigned long elapsed = millis() - transitionStartTime;
  float progress = (float)elapsed / (float)MODE_TRANSITION_MS;

  if (progress >= 1.0) {
    // Transition complete - snap to target
    currentWhitePWM = targetWhitePWM;
    currentWarmPWM = targetWarmPWM;
    isTransitioning = false;
  } else {
    // Linear interpolation for smooth crossfade
    currentWhitePWM = startWhitePWM + (targetWhitePWM - startWhitePWM) * progress;
    currentWarmPWM = startWarmPWM + (targetWarmPWM - startWarmPWM) * progress;
  }

  // Apply current PWM values to LEDs
  analogWrite(WHITE_LED_PIN, (uint8_t)currentWhitePWM);
  analogWrite(WARM_LED_PIN, (uint8_t)currentWarmPWM);
}
