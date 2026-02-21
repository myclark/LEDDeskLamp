#include "led_control.h"
#include "battery_monitor.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declarations
static void flashBoundaryIndicator();
static uint8_t getCompensatedPWM(uint8_t brightnessLevel);

// LEDC channels for ESP32 PWM
#define WHITE_LED_CHANNEL 0
#define WARM_LED_CHANNEL  1
#define PWM_FREQUENCY 5000  // 5 kHz
#define PWM_RESOLUTION 8    // 8-bit (0-255)

// Exported state variables
LampState currentLampState = OFF;
uint8_t currentMode = MODE_WARM;
uint8_t brightness = MAX_BRIGHTNESS;
int8_t brightnessDirection = -1;           // -1 = dimming, +1 = brightening

// Gamma LUT
static uint8_t gammaLUT[MAX_BRIGHTNESS + 1];

// Smooth mode transition state
static float currentWhitePWM = 0.0;
static float currentWarmPWM  = 0.0;
static float targetWhitePWM  = 0.0;
static float targetWarmPWM   = 0.0;
static float startWhitePWM   = 0.0;
static float startWarmPWM    = 0.0;
static unsigned long transitionStartTime = 0;
static bool isTransitioning = false;

// Battery indicator state
static bool indicatorPlaying = false;
static uint8_t indicatorPulseCount = 0;
static uint16_t indicatorPeriodMs = 0;
static float indicatorSharpness = 1.0;
static unsigned long indicatorStartTime = 0;
static uint8_t indicatorBrightness = 0;
static uint8_t indicatorLEDChannel = 0;

void initLED() {
  ledcSetup(WHITE_LED_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(WARM_LED_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);

  ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
  ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);

  ledcWrite(WHITE_LED_CHANNEL, 0);
  ledcWrite(WARM_LED_CHANNEL, 0);

  calculateGammaLUT();

  DEBUG_PRINTLN("LED control initialized (LEDC PWM)");
  DEBUG_PRINT("Continuous brightness (0-");
  DEBUG_PRINT(MAX_BRIGHTNESS);
  DEBUG_PRINT("), Gamma: ");
  DEBUG_PRINTLN(GAMMA_CORRECTION);
}

void turnOn(uint8_t mode, uint8_t brightnessLevel) {
  // Re-attach PWM pins when coming from OFF (they may have been detached)
  if (currentLampState == OFF) {
    ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
    ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);
    DEBUG_PRINTLN("Transitioning from OFF: PWM re-attached");
  }

  currentLampState = ON;
  currentMode = mode;
  brightness = brightnessLevel;

  uint8_t pwmValue = getCompensatedPWM(brightness);

  if (mode == MODE_COOL) {
    targetWhitePWM = pwmValue;
    targetWarmPWM = 0;
  } else {
    targetWhitePWM = 0;
    targetWarmPWM = pwmValue;
  }

  startWhitePWM = currentWhitePWM;
  startWarmPWM = currentWarmPWM;
  transitionStartTime = millis();
  isTransitioning = true;

  DEBUG_PRINT("ON: mode=");
  DEBUG_PRINT(mode == MODE_COOL ? "COOL" : "WARM");
  DEBUG_PRINT(", brightness=");
  DEBUG_PRINTLN(brightnessLevel);
}

void turnOff() {
  currentLampState = OFF;
  targetWhitePWM = 0;
  targetWarmPWM = 0;
  startWhitePWM = currentWhitePWM;
  startWarmPWM = currentWarmPWM;
  transitionStartTime = millis();
  isTransitioning = true;
  DEBUG_PRINTLN("State: OFF");
}

void swapMode(uint8_t newMode, uint8_t newBrightness) {
  if (currentLampState != ON) return;

  currentMode = newMode;
  brightness = newBrightness;

  uint8_t pwmValue = getCompensatedPWM(brightness);

  if (newMode == MODE_COOL) {
    targetWhitePWM = pwmValue;
    targetWarmPWM = 0;
  } else {
    targetWhitePWM = 0;
    targetWarmPWM = pwmValue;
  }

  startWhitePWM = currentWhitePWM;
  startWarmPWM = currentWarmPWM;
  transitionStartTime = millis();
  isTransitioning = true;

  DEBUG_PRINT("Mode swap to: ");
  DEBUG_PRINTLN(newMode == MODE_COOL ? "COOL" : "WARM");
}

void incrementBrightness() {
  if (currentLampState != ON) return;

  uint8_t oldBrightness = brightness;
  uint8_t effectiveMaxBrightness = getBatteryLimitedMaxBrightness();

  int newBrightness = (int)brightness + brightnessDirection;

  if (newBrightness >= effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    if (oldBrightness != effectiveMaxBrightness) {
      DEBUG_PRINTLN("(Reached MAX - release and hold again to dim)");
      flashBoundaryIndicator();
    }
  } else if (newBrightness <= 1) {
    brightness = 1;
    if (oldBrightness != 1) {
      DEBUG_PRINTLN("(Reached MIN - release and hold again to brighten)");
      flashBoundaryIndicator();
    }
  } else {
    brightness = (uint8_t)newBrightness;
  }

  // Clamp if battery state changed while dimming
  if (brightness > effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    DEBUG_PRINT("Brightness clamped to ");
    DEBUG_PRINT(effectiveMaxBrightness);
    DEBUG_PRINTLN(" (battery CRITICAL)");
  }

  // Cancel any ongoing mode transition, update PWM directly
  isTransitioning = false;

  uint8_t pwmValue = getCompensatedPWM(brightness);
  if (currentMode == MODE_COOL) {
    currentWhitePWM = pwmValue;
    targetWhitePWM = pwmValue;
    ledcWrite(WHITE_LED_CHANNEL, pwmValue);
    ledcWrite(WARM_LED_CHANNEL, 0);
    currentWarmPWM = 0;
    targetWarmPWM = 0;
  } else {
    currentWarmPWM = pwmValue;
    targetWarmPWM = pwmValue;
    ledcWrite(WHITE_LED_CHANNEL, 0);
    ledcWrite(WARM_LED_CHANNEL, pwmValue);
    currentWhitePWM = 0;
    targetWhitePWM = 0;
  }
}

void reverseBrightnessDirection() {
  brightnessDirection *= -1;
  DEBUG_PRINTLN("Direction reversed for next hold");
}

uint8_t getActiveBrightness() {
  return brightness;
}

static void flashBoundaryIndicator() {
  uint8_t activeLEDChannel = (currentMode == MODE_COOL) ? WHITE_LED_CHANNEL : WARM_LED_CHANNEL;
  uint8_t currentPWM = getCompensatedPWM(brightness);

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
  for (int i = 0; i <= MAX_BRIGHTNESS; i++) {
    float linearBrightness = (float)i / (float)MAX_BRIGHTNESS;
    float corrected = pow(linearBrightness, GAMMA_CORRECTION);
    uint8_t pwmValue = (uint8_t)(corrected * MAX_BRIGHTNESS);

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

static uint8_t getCompensatedPWM(uint8_t brightnessLevel) {
  uint8_t basePWM = gammaLUT[brightnessLevel];

  float factor = getBrightnessCompensationFactor();
  uint8_t compensatedPWM = (uint8_t)(basePWM * factor);

  if (brightnessLevel > 0 && compensatedPWM < MIN_BRIGHTNESS_PWM) {
    compensatedPWM = MIN_BRIGHTNESS_PWM;
  }

  return compensatedPWM;
}

void updateModeTransition() {
  if (!isTransitioning) return;

  unsigned long elapsed = millis() - transitionStartTime;
  float progress = (float)elapsed / (float)MODE_TRANSITION_MS;

  if (progress >= 1.0) {
    progress = 1.0;
    isTransitioning = false;
  }

  currentWhitePWM = startWhitePWM + (targetWhitePWM - startWhitePWM) * progress;
  currentWarmPWM  = startWarmPWM  + (targetWarmPWM  - startWarmPWM)  * progress;

  ledcWrite(WHITE_LED_CHANNEL, (uint8_t)currentWhitePWM);
  ledcWrite(WARM_LED_CHANNEL,  (uint8_t)currentWarmPWM);

  // When transition to OFF completes, detach PWM and force GPIO LOW
  if (!isTransitioning && currentLampState == OFF) {
    if (currentWhitePWM == 0 && currentWarmPWM == 0) {
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

void playBatteryIndicator(BatteryState state) {
  switch (state) {
    case BATTERY_NORMAL:
      indicatorPulseCount = PULSE_NORMAL_COUNT;
      indicatorPeriodMs   = PULSE_NORMAL_PERIOD_MS;
      indicatorSharpness  = PULSE_NORMAL_SHARPNESS;
      break;
    case BATTERY_LOW:
      indicatorPulseCount = PULSE_LOW_COUNT;
      indicatorPeriodMs   = PULSE_LOW_PERIOD_MS;
      indicatorSharpness  = PULSE_LOW_SHARPNESS;
      break;
    case BATTERY_CRITICAL:
    case BATTERY_CUTOFF:
      indicatorPulseCount = PULSE_CRITICAL_COUNT;
      indicatorPeriodMs   = PULSE_CRITICAL_PERIOD_MS;
      indicatorSharpness  = PULSE_CRITICAL_SHARPNESS;
      break;
  }

  // Use saved brightness but enforce minimum
  indicatorBrightness = (brightness > PULSE_MIN_BRIGHTNESS) ? brightness : PULSE_MIN_BRIGHTNESS;
  indicatorLEDChannel = (currentMode == MODE_COOL) ? WHITE_LED_CHANNEL : WARM_LED_CHANNEL;

  // Cancel any in-progress mode transition and zero both channels
  isTransitioning = false;
  currentWhitePWM = 0;
  currentWarmPWM  = 0;
  ledcWrite(WHITE_LED_CHANNEL, 0);
  ledcWrite(WARM_LED_CHANNEL,  0);

  indicatorStartTime = millis();
  indicatorPlaying = true;

  DEBUG_PRINTLN("Battery indicator started");
}

void updateBatteryIndicator() {
  if (!indicatorPlaying) return;

  unsigned long totalElapsed = millis() - indicatorStartTime;
  uint8_t currentPulseIndex = (uint8_t)(totalElapsed / indicatorPeriodMs);

  if (currentPulseIndex >= indicatorPulseCount) {
    // Animation complete - restore lamp to normal brightness
    indicatorPlaying = false;

    if (currentLampState == ON) {
      uint8_t pwm = getCompensatedPWM(brightness);
      if (currentMode == MODE_COOL) {
        currentWhitePWM = pwm;
        targetWhitePWM  = pwm;
        currentWarmPWM  = 0;
        targetWarmPWM   = 0;
        ledcWrite(WHITE_LED_CHANNEL, pwm);
        ledcWrite(WARM_LED_CHANNEL,  0);
      } else {
        currentWarmPWM  = pwm;
        targetWarmPWM   = pwm;
        currentWhitePWM = 0;
        targetWhitePWM  = 0;
        ledcWrite(WHITE_LED_CHANNEL, 0);
        ledcWrite(WARM_LED_CHANNEL,  pwm);
      }
    }

    DEBUG_PRINTLN("Battery indicator complete");
    return;
  }

  // Calculate position within current pulse (0.0 to 1.0)
  unsigned long pulseElapsed = totalElapsed - ((unsigned long)currentPulseIndex * indicatorPeriodMs);
  float t = (float)pulseElapsed / (float)indicatorPeriodMs;
  if (t > 1.0f) t = 1.0f;

  // Envelope: pow(sin(t * PI), sharpness) — sine gives smooth ramp, higher sharpness = sharper peak
  float envelope = powf(sinf(t * (float)M_PI), indicatorSharpness);
  if (envelope < 0.0f) envelope = 0.0f;

  uint8_t scaledBrightness = (uint8_t)(indicatorBrightness * envelope);
  uint8_t pwm = getCompensatedPWM(scaledBrightness);
  ledcWrite(indicatorLEDChannel, pwm);
}

bool isPlayingIndicator() {
  return indicatorPlaying;
}
