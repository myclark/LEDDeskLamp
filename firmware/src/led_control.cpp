#include "led_control.h"
#include "battery_monitor.h"

// Forward declaration
static void flashBoundaryIndicator();

// LEDC channels for ESP32 PWM
#define WHITE_LED_CHANNEL 0
#define WARM_LED_CHANNEL 1
#define PWM_FREQUENCY 5000  // 5 kHz
#define PWM_RESOLUTION 8    // 8-bit (0-255)

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
  // Configure LEDC PWM channels for ESP32
  ledcSetup(WHITE_LED_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(WARM_LED_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);

  // Attach pins to PWM channels
  ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
  ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);

  // Initialize to off
  ledcWrite(WHITE_LED_CHANNEL, 0);
  ledcWrite(WARM_LED_CHANNEL, 0);

  // Calculate gamma-corrected lookup table
  calculateGammaLUT();

  // Initialize LEDs to OFF state
  updateLED();

  DEBUG_PRINTLN("LED control initialized (LEDC PWM)");
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

  // If transitioning FROM OFF state, re-attach PWM pins
  static LampState previousState = OFF;
  if (previousState == OFF && currentLampState != OFF) {
    ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
    ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);
    DEBUG_PRINTLN("Transitioning from OFF: PWM re-attached");
  }
  previousState = currentLampState;

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

  // Get battery-limited max brightness (may be reduced in CRITICAL state)
  uint8_t effectiveMaxBrightness = getBatteryLimitedMaxBrightness();

  // Continuously increment/decrement brightness
  int newBrightness = (int)brightness + brightnessDirection;

  // Clamp at boundaries (1 to effectiveMaxBrightness) and flash
  // Note: brightness=1 outputs MIN_BRIGHTNESS_PWM from LUT
  if (newBrightness >= effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    if (oldBrightness != effectiveMaxBrightness) {
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

  // Clamp existing brightness if it exceeds new max (battery went to CRITICAL)
  if (brightness > effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    DEBUG_PRINT("Brightness clamped to ");
    DEBUG_PRINT(effectiveMaxBrightness);
    DEBUG_PRINTLN(" (battery CRITICAL)");
  }

  // Cancel any ongoing mode transition and update directly
  isTransitioning = false;

  // Directly update PWM (no transition for continuous brightness changes)
  uint8_t pwmValue = gammaLUT[brightness];
  if (currentLampState == WHITE) {
    currentWhitePWM = pwmValue;
    targetWhitePWM = pwmValue;
    ledcWrite(WHITE_LED_CHANNEL, pwmValue);
    ledcWrite(WARM_LED_CHANNEL, 0);  // Ensure other LED is off
  } else if (currentLampState == WARM) {
    currentWarmPWM = pwmValue;
    targetWarmPWM = pwmValue;
    ledcWrite(WHITE_LED_CHANNEL, 0);  // Ensure other LED is off
    ledcWrite(WARM_LED_CHANNEL, pwmValue);
  }
}

void flashBoundaryIndicator() {
  // Double flash to indicate min/max boundary (more deliberate than single flash)
  uint8_t activeLEDChannel = (currentLampState == WHITE) ? WHITE_LED_CHANNEL : WARM_LED_CHANNEL;
  uint8_t currentPWM = gammaLUT[brightness];

  // First flash
  ledcWrite(activeLEDChannel, 0);
  delay(80);
  ledcWrite(activeLEDChannel, currentPWM);
  delay(80);

  // Second flash
  ledcWrite(activeLEDChannel, 0);
  delay(80);
  ledcWrite(activeLEDChannel, currentPWM);
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
    progress = 1.0;
    isTransitioning = false;
  }

  // Always interpolate for smooth crossfade
  currentWhitePWM = startWhitePWM + (targetWhitePWM - startWhitePWM) * progress;
  currentWarmPWM = startWarmPWM + (targetWarmPWM - startWarmPWM) * progress;

  // Apply current PWM values to both LEDs using LEDC
  ledcWrite(WHITE_LED_CHANNEL, (uint8_t)currentWhitePWM);
  ledcWrite(WARM_LED_CHANNEL, (uint8_t)currentWarmPWM);

  // CRITICAL FIX: When transition to OFF is complete, detach PWM and force GPIO LOW
  // This ensures true 0V output instead of relying on PWM=0 which may have leakage
  if (!isTransitioning && currentLampState == OFF) {
    if (currentWhitePWM == 0 && currentWarmPWM == 0) {
      // Detach LEDC PWM and set pins to OUTPUT LOW
      ledcDetachPin(WHITE_LED_PIN);
      ledcDetachPin(WARM_LED_PIN);
      pinMode(WHITE_LED_PIN, OUTPUT);
      pinMode(WARM_LED_PIN, OUTPUT);
      digitalWrite(WHITE_LED_PIN, LOW);
      digitalWrite(WARM_LED_PIN, LOW);
      DEBUG_PRINTLN("OFF: PWM detached, pins forced LOW");
    }
  }
}

void flashLowBatteryWarning() {
  // Triple flash to indicate low battery (only when lamp is ON)
  if (currentLampState == OFF) {
    return;  // Don't flash when lamp is off
  }

  uint8_t activeLEDChannel = (currentLampState == WHITE) ? WHITE_LED_CHANNEL : WARM_LED_CHANNEL;
  uint8_t currentPWM = gammaLUT[brightness];

  // Save current transition state and cancel it
  bool wasTransitioning = isTransitioning;
  isTransitioning = false;

  // Triple flash pattern (more urgent than boundary double flash)
  for (int i = 0; i < 3; i++) {
    ledcWrite(activeLEDChannel, 0);
    delay(100);
    ledcWrite(activeLEDChannel, currentPWM);
    delay(100);
  }

  // Restore transition state if needed
  isTransitioning = wasTransitioning;

  DEBUG_PRINTLN("Low battery warning flashed");
}
