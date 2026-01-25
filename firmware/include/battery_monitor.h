#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <Arduino.h>

// Battery state enum
enum BatteryState {
  BATTERY_NORMAL,    // > 3.5V
  BATTERY_LOW,       // 3.2V - 3.5V (flash on wake only)
  BATTERY_CRITICAL,  // 3.0V - 3.2V (limit brightness, flash every mode change)
  BATTERY_CUTOFF     // < 3.0V (refuse to turn on)
};

// Initialize battery monitoring (call from setup)
void initBatteryMonitor();

// Read current battery voltage
float readBatteryVoltage();

// Get current battery state (state machine with hysteresis)
BatteryState getBatteryState();

// Update battery monitor (call from loop)
void updateBatteryMonitor();

// Display battery status to serial
void displayBatteryStatus();

// Get last voltage reading (cached)
float getLastBatteryVoltage();

// Get effective max brightness based on battery state
uint8_t getBatteryLimitedMaxBrightness();

// Get brightness compensation factor for current battery voltage
// Returns a factor (0.0 to 1.0) to scale PWM values
// At reference voltage (3.5V), returns 1.0
// At higher voltages, returns < 1.0 to maintain consistent perceived brightness
float getBrightnessCompensationFactor();

// For unit testing: Calculate compensation factor for a given voltage
float calculateCompensationFactor(float voltage);

// For unit testing: Update state machine with given voltage (bypasses timing)
void updateBatteryStateMachine(float batteryVoltage);

#endif
