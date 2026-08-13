#include "pot_input.h"

static bool potOnState = false;
static uint8_t potBrightnessTarget = 0;

void initPotInput() {
  pinMode(POT_PIN, INPUT);
  // battery_monitor's initBatteryMonitor() already configures these globally, but set
  // them here too so pot behavior doesn't depend on module init order.
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  DEBUG_PRINTLN("Pot input initialized");
}

// Pure mapping, no hardware access — testable directly.
uint8_t mapPotToBrightness(int rawAdc) {
  if (rawAdc < 0) rawAdc = 0;
  if (rawAdc > POT_ADC_MAX) rawAdc = POT_ADC_MAX;
  return (uint8_t)(((long)rawAdc * MAX_BRIGHTNESS) / POT_ADC_MAX);
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

static int readPotRawAveraged() {
  long sum = 0;
  for (int i = 0; i < POT_SAMPLE_COUNT; i++) {
    sum += analogRead(POT_PIN);
  }
  return (int)(sum / POT_SAMPLE_COUNT);
}

void updatePotInput() {
  int raw = readPotRawAveraged();
  potBrightnessTarget = mapPotToBrightness(raw);
  potOnState = updatePotStateMachine(potOnState, potBrightnessTarget);
}

bool isPotRequestingOn() { return potOnState; }
uint8_t getPotBrightnessTarget() { return potBrightnessTarget; }
