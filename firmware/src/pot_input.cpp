#include "pot_input.h"
#include <math.h>

static bool potOnState = false;
static uint8_t potBrightnessTarget = 0;

// Tick-to-tick noise filter state (see smoothPotReading())
static float filteredRaw = 0.0f;
static unsigned long lastFilterUpdateTime = 0;
static bool filterInitialized = false;

void initPotInput() {
  pinMode(POT_POWER_PIN, OUTPUT);
  potPowerOn();
  pinMode(POT_PIN, INPUT);
  // battery_monitor's initBatteryMonitor() already configures these globally, but set
  // them here too so pot behavior doesn't depend on module init order.
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  filterInitialized = false;
  DEBUG_PRINTLN("Pot input initialized");
}

// Direct GPIO drive, no external switch transistor — see the POT_POWER_PIN comment in
// config.h for why this is electrically fine at the pot's ~330µA draw.
void potPowerOn() { digitalWrite(POT_POWER_PIN, HIGH); }
void potPowerOff() { digitalWrite(POT_POWER_PIN, LOW); }

// Pure mapping, no hardware access — testable directly. Snaps to the extremes within
// POT_MAX_DEADZONE of the top so the user can reliably reach exactly MAX_BRIGHTNESS even
// if the pot's mechanical/electrical range doesn't quite hit full scale. The bottom end
// doesn't need an equivalent snap here — POT_ON_HYSTERESIS already puts a floor under the
// lowest brightness reachable while ON, via updatePotStateMachine() below.
uint8_t mapPotToBrightness(int rawAdc) {
  if (rawAdc < 0) rawAdc = 0;
  if (rawAdc > POT_ADC_MAX) rawAdc = POT_ADC_MAX;
  uint8_t mapped = (uint8_t)(((long)rawAdc * MAX_BRIGHTNESS) / POT_ADC_MAX);
  if (mapped >= MAX_BRIGHTNESS - POT_MAX_DEADZONE) {
    mapped = MAX_BRIGHTNESS;
  }
  return mapped;
}

// Pure hysteresis state machine, no hardware access — testable directly.
// Two thresholds prevent flicker at the boundary: once ON, must drop to/below
// POT_OFF_THRESHOLD to turn off; once OFF, must rise to/above the higher
// POT_ON_HYSTERESIS to turn back on. In between, holds the last state.
bool updatePotStateMachine(bool currentlyOn, uint8_t mappedBrightness) {
  if (currentlyOn) {
    return mappedBrightness > POT_OFF_THRESHOLD;
  }
  return mappedBrightness >= POT_ON_HYSTERESIS;
}

// Pure exponential smoothing step, no hardware access — testable directly with an
// explicit dt instead of relying on millis(). Filters ADC noise at any dial position,
// not just the endpoints (those get their own dead zones above).
float smoothPotReading(float current, float rawTarget, unsigned long dtMs) {
  if (dtMs == 0) return current;
  float alpha = 1.0f - expf(-(float)dtMs / POT_FILTER_TIME_CONSTANT_MS);
  return current + (rawTarget - current) * alpha;
}

static int readPotRawAveraged() {
  long sum = 0;
  for (int i = 0; i < POT_SAMPLE_COUNT; i++) {
    sum += analogRead(POT_PIN);
  }
  return (int)(sum / POT_SAMPLE_COUNT);
}

void updatePotInput() {
  int raw = readPotRawAveraged();
  unsigned long now = millis();

  if (!filterInitialized) {
    filteredRaw = (float)raw;
    filterInitialized = true;
  } else {
    filteredRaw = smoothPotReading(filteredRaw, (float)raw, now - lastFilterUpdateTime);
  }
  lastFilterUpdateTime = now;

  potBrightnessTarget = mapPotToBrightness((int)(filteredRaw + 0.5f));
  potOnState = updatePotStateMachine(potOnState, potBrightnessTarget);
}

bool isPotRequestingOn() { return potOnState; }
uint8_t getPotBrightnessTarget() { return potBrightnessTarget; }
