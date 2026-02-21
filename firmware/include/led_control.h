#ifndef LED_CONTROL_H
#define LED_CONTROL_H

#include <Arduino.h>
#include "config.h"
#include "battery_monitor.h"

enum LampState { OFF, ON };

extern LampState currentLampState;
extern uint8_t currentMode;           // MODE_WARM or MODE_COOL
extern uint8_t brightness;            // 0 to MAX_BRIGHTNESS
extern int8_t brightnessDirection;    // -1 or +1

void initLED();

// Power control
void turnOn(uint8_t mode, uint8_t brightnessLevel);
void turnOff();

// Mode swap with crossfade
void swapMode(uint8_t newMode, uint8_t newBrightness);

// Brightness
void incrementBrightness();
void reverseBrightnessDirection();
uint8_t getActiveBrightness();

// Smooth transition (call from loop)
void updateModeTransition();

// Battery indicator pulse (non-blocking)
void playBatteryIndicator(BatteryState state);
void updateBatteryIndicator();    // Call from loop
bool isPlayingIndicator();

void calculateGammaLUT();

#endif // LED_CONTROL_H
