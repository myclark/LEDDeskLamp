#include "led_control.h"
#include "battery_monitor.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declarations
static void triggerBoundaryFlash();
static uint16_t getCompensatedPWM(uint16_t brightnessLevel);
static uint16_t clampToBatteryLimit(uint16_t requested);

// LEDC channels for ESP32 PWM
#define WHITE_LED_CHANNEL 0
#define WARM_LED_CHANNEL  1
#define PWM_FREQUENCY 5000  // 5 kHz
// PWM hardware duty resolution. Deliberately equal to MAX_BRIGHTNESS (config.h) — the whole
// pipeline from the pot's ADC read through to this final PWM write is now the same 12-bit
// width end to end (ADC -> brightness -> gamma LUT -> PWM duty), so nothing downstream of
// the ADC ever throws away resolution the pot actually provided. 12 bits (0-4095) is also
// well inside the LEDC hardware constraint at this frequency (freq * 2^bits <= ~80 MHz APB
// clock -> up to ~13 bits at 5 kHz).
#define PWM_RESOLUTION 12
#define PWM_MAX_DUTY ((1u << PWM_RESOLUTION) - 1)
// MIN_BRIGHTNESS_PWM (config.h) is expressed in MAX_BRIGHTNESS-equivalent units for
// readability; scale it to the actual PWM duty resolution here so it stays meaningful even
// if PWM_RESOLUTION and MAX_BRIGHTNESS ever diverge (today they're equal, so this is a
// no-op multiply/divide by the same value).
static const uint16_t MIN_PWM_DUTY =
    ((uint32_t)MIN_BRIGHTNESS_PWM * PWM_MAX_DUTY) / MAX_BRIGHTNESS;

// Exported state variables
LampState currentLampState = OFF;
uint8_t currentMode = MODE_WARM;
uint16_t brightness = MAX_BRIGHTNESS;
int8_t brightnessDirection = -1;           // -1 = dimming, +1 = brightening

// Gamma LUT — indexed by the full-resolution "brightness" level (0-MAX_BRIGHTNESS, 12-bit),
// storing the full-resolution PWM duty (0-PWM_MAX_DUTY, see above). Computed once in
// calculateGammaLUT() using float math for precision; the table itself just caches those
// 4096 results so getCompensatedPWM() doesn't call pow() on every PWM update.
static uint16_t gammaLUT[MAX_BRIGHTNESS + 1];

// Smooth mode transition state
static float currentWhitePWM = 0.0;
static float currentWarmPWM  = 0.0;
static float targetWhitePWM  = 0.0;
static float targetWarmPWM   = 0.0;
static float startWhitePWM   = 0.0;
static float startWarmPWM    = 0.0;
static unsigned long transitionStartTime = 0;
static bool isTransitioning = false;

// Boundary flash state (non-blocking double-flash at brightness limits)
static bool boundaryFlashActive = false;
static uint8_t boundaryFlashStep = 0;
static unsigned long boundaryFlashTimer = 0;
static uint16_t boundaryFlashPWM = 0;
static uint8_t boundaryFlashChannel = 0;

// Continuous brightness slew state (potentiometer input)
static uint16_t brightnessSlewTarget = 0;
static float brightnessSlewCurrent = 0.0f;
static unsigned long lastSlewUpdateTime = 0;
static bool slewInitialized = false;

// Battery indicator state
static bool indicatorPlaying = false;
static uint8_t indicatorPulseCount = 0;
static uint16_t indicatorPeriodMs = 0;
static float indicatorSharpness = 1.0;
static unsigned long indicatorStartTime = 0;
static uint16_t indicatorBrightness = 0;
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

void turnOn(uint8_t mode, uint16_t brightnessLevel) {
  // Re-attach PWM pins when coming from OFF (they may have been detached)
  if (currentLampState == OFF) {
    ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
    ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);
    DEBUG_PRINTLN("Transitioning from OFF: PWM re-attached");
  }

  currentLampState = ON;
  currentMode = mode;
  brightness = clampToBatteryLimit(brightnessLevel);

  uint16_t pwmValue = getCompensatedPWM(brightness);

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
  DEBUG_PRINTLN(brightness);
}

void turnOnAtZero(uint8_t mode) {
  if (currentLampState == OFF) {
    ledcAttachPin(WHITE_LED_PIN, WHITE_LED_CHANNEL);
    ledcAttachPin(WARM_LED_PIN, WARM_LED_CHANNEL);
    DEBUG_PRINTLN("Transitioning from OFF: PWM re-attached");
  }

  currentLampState = ON;
  currentMode = mode;
  brightness = 0;

  currentWhitePWM = 0;
  currentWarmPWM = 0;
  targetWhitePWM = 0;
  targetWarmPWM = 0;
  isTransitioning = false;  // Nothing to crossfade from 0 to 0 — caller ramps via slew instead

  ledcWrite(WHITE_LED_CHANNEL, 0);
  ledcWrite(WARM_LED_CHANNEL, 0);

  DEBUG_PRINT("ON (at zero, ramping via slew): mode=");
  DEBUG_PRINTLN(mode == MODE_COOL ? "COOL" : "WARM");
}

void turnOff() {
  currentLampState = OFF;
  targetWhitePWM = 0;
  targetWarmPWM = 0;
  startWhitePWM = currentWhitePWM;
  startWarmPWM = currentWarmPWM;
  transitionStartTime = millis();
  isTransitioning = true;
  slewInitialized = false;  // Re-seed brightness slew cleanly on the next turnOn()
  DEBUG_PRINTLN("State: OFF");
}

void swapMode(uint8_t newMode, uint16_t newBrightness) {
  if (currentLampState != ON) return;

  currentMode = newMode;
  brightness = clampToBatteryLimit(newBrightness);

  uint16_t pwmValue = getCompensatedPWM(brightness);

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

  uint16_t oldBrightness = brightness;
  uint16_t effectiveMaxBrightness = getBatteryLimitedMaxBrightness();

  // Step by BRIGHTNESS_STEP_SIZE units, not a literal 1 — brightness lives in a much wider
  // domain now (config.h), so a single-unit step would make button-mode long-press dimming
  // 16x slower than before for the same hold duration.
  int newBrightness = (int)brightness + ((int)brightnessDirection * BRIGHTNESS_STEP_SIZE);

  if (newBrightness >= effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    if (oldBrightness != effectiveMaxBrightness) {
      DEBUG_PRINTLN("(Reached MAX - release and hold again to dim)");
      triggerBoundaryFlash();
    }
  } else if (newBrightness <= BRIGHTNESS_STEP_SIZE) {
    brightness = BRIGHTNESS_STEP_SIZE;
    if (oldBrightness != BRIGHTNESS_STEP_SIZE) {
      DEBUG_PRINTLN("(Reached MIN - release and hold again to brighten)");
      triggerBoundaryFlash();
    }
  } else {
    brightness = (uint16_t)newBrightness;
  }

  // Clamp if battery state changed while dimming
  if (brightness > effectiveMaxBrightness) {
    brightness = effectiveMaxBrightness;
    DEBUG_PRINT("Brightness clamped to ");
    DEBUG_PRINT(effectiveMaxBrightness);
    DEBUG_PRINTLN(" (battery CRITICAL)");
  }

  // Skip direct PWM update while boundary flash is animating
  if (boundaryFlashActive) return;

  // Cancel any ongoing mode transition, update PWM directly
  isTransitioning = false;

  uint16_t pwmValue = getCompensatedPWM(brightness);
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

uint16_t getActiveBrightness() {
  return brightness;
}

static void triggerBoundaryFlash() {
  boundaryFlashChannel = (currentMode == MODE_COOL) ? WHITE_LED_CHANNEL : WARM_LED_CHANNEL;
  boundaryFlashPWM = getCompensatedPWM(brightness);
  boundaryFlashStep = 0;
  boundaryFlashTimer = millis();
  boundaryFlashActive = true;
  ledcWrite(boundaryFlashChannel, 0);  // Begin with LED off
}

bool isBoundaryFlashing() {
  return boundaryFlashActive;
}

void calculateGammaLUT() {
  for (int i = 0; i <= MAX_BRIGHTNESS; i++) {
    float linearBrightness = (float)i / (float)MAX_BRIGHTNESS;
    float corrected = pow(linearBrightness, GAMMA_CORRECTION);
    uint16_t pwmValue = (uint16_t)(corrected * PWM_MAX_DUTY + 0.5f);

    if (i > 0 && pwmValue < MIN_PWM_DUTY) {
      pwmValue = MIN_PWM_DUTY;
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

static uint16_t getCompensatedPWM(uint16_t brightnessLevel) {
  uint16_t basePWM = gammaLUT[brightnessLevel];

  float factor = getBrightnessCompensationFactor();
  uint16_t compensatedPWM = (uint16_t)(basePWM * factor + 0.5f);

  if (brightnessLevel > 0 && compensatedPWM < MIN_PWM_DUTY) {
    compensatedPWM = MIN_PWM_DUTY;
  }

  return compensatedPWM;
}

// Clamps a requested brightness to the current battery-imposed ceiling
// (LOW_MAX_BRIGHTNESS / CRITICAL_MAX_BRIGHTNESS, config.h, via getBatteryLimitedMaxBrightness()
// in battery_monitor.cpp) — the single point every brightness-setting call site funnels
// through (turnOn(), swapMode(), updateBrightnessSlew()), so the ceiling applies the same way
// no matter which input mode or code path requested the brightness. incrementBrightness()
// (button mode) has its own equivalent logic inline, since it also needs to know *whether*
// the request hit the ceiling in order to trigger the boundary flash.
static uint16_t clampToBatteryLimit(uint16_t requested) {
  uint16_t limit = getBatteryLimitedMaxBrightness();
  return (requested > limit) ? limit : requested;
}

void updateModeTransition() {
  // Boundary flash state machine: off→on→off→on over 4 × BOUNDARY_FLASH_STEP_MS
  if (boundaryFlashActive) {
    if (millis() - boundaryFlashTimer >= BOUNDARY_FLASH_STEP_MS) {
      boundaryFlashTimer = millis();
      boundaryFlashStep++;
      if (boundaryFlashStep > 3) {
        boundaryFlashActive = false;
        // Restore lamp brightness and sync internal state
        isTransitioning = false;
        uint16_t pwm = getCompensatedPWM(brightness);
        if (currentMode == MODE_COOL) {
          currentWhitePWM = pwm; targetWhitePWM = pwm;
          currentWarmPWM  = 0;   targetWarmPWM  = 0;
        } else {
          currentWarmPWM  = pwm; targetWarmPWM  = pwm;
          currentWhitePWM = 0;   targetWhitePWM = 0;
        }
        ledcWrite(boundaryFlashChannel, pwm);
      } else {
        // Odd steps: on; even steps: off
        ledcWrite(boundaryFlashChannel, (boundaryFlashStep % 2 == 1) ? boundaryFlashPWM : 0);
      }
    }
    return;  // Don't run mode transition while flashing
  }

  if (!isTransitioning) return;

  unsigned long elapsed = millis() - transitionStartTime;
  float progress = (float)elapsed / (float)MODE_TRANSITION_MS;

  if (progress >= 1.0) {
    progress = 1.0;
    isTransitioning = false;
  }

  currentWhitePWM = startWhitePWM + (targetWhitePWM - startWhitePWM) * progress;
  currentWarmPWM  = startWarmPWM  + (targetWarmPWM  - startWarmPWM)  * progress;

  ledcWrite(WHITE_LED_CHANNEL, (uint16_t)currentWhitePWM);
  ledcWrite(WARM_LED_CHANNEL,  (uint16_t)currentWarmPWM);

  // When transition to OFF completes, detach PWM and force GPIO LOW
  if (!isTransitioning && currentLampState == OFF) {
    if (currentWhitePWM < 0.1f && currentWarmPWM < 0.1f) {
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

void setBrightnessTarget(uint16_t target) {
  brightnessSlewTarget = target;
  if (!slewInitialized) {
    // Seed from the current brightness so the very first call doesn't visibly jump or
    // slew from zero (e.g. right after turnOn(), which already set `brightness` directly).
    brightnessSlewCurrent = brightness;
    lastSlewUpdateTime = millis();
    slewInitialized = true;
  }
}

void updateBrightnessSlew() {
  // Let the power-on crossfade and boundary flash own the PWM output while they're active.
  if (!slewInitialized || currentLampState != ON || isTransitioning || boundaryFlashActive) return;

  unsigned long now = millis();
  unsigned long dt = now - lastSlewUpdateTime;
  lastSlewUpdateTime = now;
  if (dt == 0) return;

  float alpha = 1.0f - expf(-(float)dt / BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  brightnessSlewCurrent += ((float)brightnessSlewTarget - brightnessSlewCurrent) * alpha;

  // brightnessSlewCurrent itself keeps easing toward wherever the pot raw-points, uncapped —
  // only the value actually applied is clamped, right here. That's what gives the "stop on
  // the dial" feel: holding the pot above the battery ceiling just holds the applied
  // brightness at the ceiling; turning back down below it, the (still-continuous) eased
  // value crosses back under the cap and tracking resumes with no jump.
  uint16_t newBrightness = clampToBatteryLimit((uint16_t)(brightnessSlewCurrent + 0.5f));
  if (newBrightness == brightness) return;

  brightness = newBrightness;
  uint16_t pwm = getCompensatedPWM(brightness);
  if (currentMode == MODE_COOL) {
    currentWhitePWM = pwm;
    targetWhitePWM  = pwm;
    ledcWrite(WHITE_LED_CHANNEL, pwm);
  } else {
    currentWarmPWM = pwm;
    targetWarmPWM  = pwm;
    ledcWrite(WARM_LED_CHANNEL, pwm);
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
      uint16_t pwm = getCompensatedPWM(brightness);
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

  uint16_t scaledBrightness = (uint16_t)(indicatorBrightness * envelope);
  uint16_t pwm = getCompensatedPWM(scaledBrightness);
  ledcWrite(indicatorLEDChannel, pwm);
}

bool isPlayingIndicator() {
  return indicatorPlaying;
}
