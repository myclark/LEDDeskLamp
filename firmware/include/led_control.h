#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>
#include "config.h"

// Lamp states
enum LampState {
  OFF,
  WHITE,
  WARM
};

// External state variables
extern LampState currentLampState;
extern uint8_t brightness;        // Continuous brightness (0 to MAX_BRIGHTNESS)
extern int8_t brightnessDirection;

// Function declarations
void initLED();
void advanceState();
void updateLED();
void incrementBrightness();         // Continuous brightness control
void calculateGammaLUT();
void updateModeTransition();        // Call from main loop for smooth mode transitions
void flashLowBatteryWarning();      // Flash LEDs to indicate low battery

#endif // LED_CONTROL_H
