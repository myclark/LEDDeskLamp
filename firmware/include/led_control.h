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

// Continuous brightness tracking (potentiometer input). setBrightnessTarget() records the
// desired brightness; updateBrightnessSlew() (call every loop) eases the live brightness
// and PWM output toward that target with exponential smoothing, so fast pot movements
// produce a smooth, slightly lagging ramp instead of an instant jump.
void setBrightnessTarget(uint8_t target);
void updateBrightnessSlew();

// Battery indicator pulse (non-blocking)
void playBatteryIndicator(BatteryState state);
void updateBatteryIndicator();    // Call from loop
bool isPlayingIndicator();

// Boundary flash (non-blocking double-flash when brightness hits min/max)
bool isBoundaryFlashing();

void calculateGammaLUT();

#endif // LED_CONTROL_H
