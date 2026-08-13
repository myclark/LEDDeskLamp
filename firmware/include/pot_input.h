#ifndef POT_INPUT_H
#define POT_INPUT_H

#include <Arduino.h>
#include "config.h"

// Potentiometer-based on/off + brightness control — the primary input when USE_POT_INPUT
// is defined (see config.h). Fully counter-clockwise requests OFF; anywhere above that
// requests ON at a brightness proportional to position. There's no persisted brightness
// state: the pot's current position *is* the requested brightness, always.

void initPotInput();
void updatePotInput();            // Call every loop() iteration

bool isPotRequestingOn();          // Current on/off request, after hysteresis
uint8_t getPotBrightnessTarget();  // Current brightness the pot is pointing at (0..MAX_BRIGHTNESS)

// Pure logic, exposed for unit testing (bypasses the real ADC read):
uint8_t mapPotToBrightness(int rawAdc);
bool updatePotStateMachine(bool currentlyOn, uint8_t mappedBrightness);

#endif // POT_INPUT_H
