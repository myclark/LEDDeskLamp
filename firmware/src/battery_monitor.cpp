#include "battery_monitor.h"
#include "config.h"

// Battery state
float lastBatteryVoltage = 0.0;
unsigned long lastBatteryReadTime = 0;
unsigned long lastBatteryDisplayTime = 0;
BatteryState currentBatteryState = BATTERY_NORMAL;
uint8_t criticalConsecutiveCount = 0;  // Hysteresis counter for entering CRITICAL state

// Voltage thresholds (volts)
#define BATTERY_FULL 4.2
#define BATTERY_NOMINAL 3.7
#define BATTERY_LOW_THRESHOLD 3.5
#define BATTERY_CRITICAL_THRESHOLD 3.2
#define BATTERY_CRITICAL_HYSTERESIS 3.3  // Return to LOW when above this (100mV hysteresis)
#define BATTERY_CUTOFF_THRESHOLD 3.0

// Brightness compensation reference voltage
// At this voltage, PWM is used at full value; above this, PWM is scaled down
#define BRIGHTNESS_REFERENCE_VOLTAGE 3.5

// Programming mode detection
// When voltage > 4.5V, we're on USB power (not battery)
// Cap PWM to 50% for safety and consistent development experience
#define PROGRAMMING_MODE_VOLTAGE 4.5
#define PROGRAMMING_MODE_MAX_PWM 128

// Voltage divider scaling: (100kΩ + 33kΩ) / 33kΩ = 4.030
#define VOLTAGE_DIVIDER_RATIO 4.030

// Timing
#define BATTERY_READ_INTERVAL_MS 30000   // Read every 30 seconds (30s × 3 = 90s for critical)
#define BATTERY_DISPLAY_INTERVAL_MS 60000 // Auto-display every 60 seconds

// Hysteresis
#define CRITICAL_CONSECUTIVE_THRESHOLD 3  // Number of consecutive low readings before entering CRITICAL

// Brightness limiting in CRITICAL state
#define CRITICAL_MAX_BRIGHTNESS 64  // 50% of normal max (128)

void initBatteryMonitor() {
  // Configure ADC resolution and attenuation
  analogReadResolution(12);  // 12-bit ADC (0-4095)

  // Set ADC attenuation to 11dB for 0-3.3V range
  // (voltage divider brings 4.2V max down to ~1.04V, well within range)
  analogSetAttenuation(ADC_11db);

  // Read initial voltage
  lastBatteryVoltage = readBatteryVoltage();
  lastBatteryReadTime = millis();
  lastBatteryDisplayTime = millis();

  DEBUG_PRINTLN("Battery monitor initialized");
  displayBatteryStatus();
}

float readBatteryVoltage() {
  // Take multiple samples and average for stability
  int sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(1);
  }
  int adcValue = sum / 8;

  // Convert ADC value to voltage
  // ESP32-C3 ADC reference is 3.3V (typically)
  // Voltage divider brings 4.2V battery down to ~1.042V at ADC input
  float adcVoltage = adcValue * (3.3 / 4095.0);
  float voltage = adcVoltage * VOLTAGE_DIVIDER_RATIO * ADC_CALIBRATION_FACTOR;

  DEBUG_PRINT("ADC raw: ");
  DEBUG_PRINT(adcValue);
  DEBUG_PRINT(", ADC voltage: ");
  DEBUG_PRINT(adcVoltage);
  DEBUG_PRINT("V, Battery (calibrated): ");
  DEBUG_PRINT(voltage);
  DEBUG_PRINTLN("V");

  return voltage;
}

BatteryState getBatteryState() {
  return currentBatteryState;
}

float getLastBatteryVoltage() {
  return lastBatteryVoltage;
}

uint8_t getBatteryLimitedMaxBrightness() {
  if (currentBatteryState == BATTERY_CRITICAL) {
    return CRITICAL_MAX_BRIGHTNESS;
  }
  return MAX_BRIGHTNESS;  // From config.h
}

float calculateCompensationFactor(float voltage) {
  // Compensate for voltage-dependent LED brightness
  // At reference voltage (3.5V), factor = 1.0 (full PWM)
  // At higher voltages, factor < 1.0 (reduced PWM to match brightness)
  // At lower voltages, factor could be > 1.0, but we cap at 1.0

  // Programming mode: USB power detected (voltage > 4.5V)
  // Use fixed factor to cap PWM at 50% for safety
  if (voltage > PROGRAMMING_MODE_VOLTAGE) {
    return (float)PROGRAMMING_MODE_MAX_PWM / 255.0;
  }

  if (voltage <= BRIGHTNESS_REFERENCE_VOLTAGE) {
    // At or below reference, use full PWM (can't compensate for low voltage)
    return 1.0;
  }

  // Linear compensation: factor = reference / actual
  float factor = BRIGHTNESS_REFERENCE_VOLTAGE / voltage;

  // Clamp to valid range (shouldn't exceed 1.0 given the check above)
  if (factor > 1.0) factor = 1.0;
  if (factor < 0.5) factor = 0.5;  // Don't reduce below 50% (safety floor)

  return factor;
}

float getBrightnessCompensationFactor() {
  // Get compensation factor based on last battery reading
  float voltage = lastBatteryVoltage + BMS_VOLTAGE_DROP;
  return calculateCompensationFactor(voltage);
}

// State machine logic (extracted for testability)
void updateBatteryStateMachine(float batteryVoltage) {
  // State machine with hysteresis
  switch (currentBatteryState) {
      case BATTERY_NORMAL:
        if (batteryVoltage < BATTERY_CUTOFF_THRESHOLD) {
          currentBatteryState = BATTERY_CUTOFF;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: NORMAL → CUTOFF");
        } else if (batteryVoltage < BATTERY_CRITICAL_THRESHOLD) {
          criticalConsecutiveCount++;
          if (criticalConsecutiveCount >= CRITICAL_CONSECUTIVE_THRESHOLD) {
            currentBatteryState = BATTERY_CRITICAL;
            DEBUG_PRINTLN("State: NORMAL → CRITICAL (hysteresis)");
          }
        } else if (batteryVoltage < BATTERY_LOW_THRESHOLD) {
          currentBatteryState = BATTERY_LOW;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: NORMAL → LOW");
        } else {
          criticalConsecutiveCount = 0;  // Reset counter
        }
        break;

      case BATTERY_LOW:
        if (batteryVoltage < BATTERY_CUTOFF_THRESHOLD) {
          currentBatteryState = BATTERY_CUTOFF;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: LOW → CUTOFF");
        } else if (batteryVoltage < BATTERY_CRITICAL_THRESHOLD) {
          criticalConsecutiveCount++;
          if (criticalConsecutiveCount >= CRITICAL_CONSECUTIVE_THRESHOLD) {
            currentBatteryState = BATTERY_CRITICAL;
            DEBUG_PRINTLN("State: LOW → CRITICAL (hysteresis)");
          }
        } else if (batteryVoltage >= BATTERY_LOW_THRESHOLD) {
          currentBatteryState = BATTERY_NORMAL;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: LOW → NORMAL");
        } else {
          criticalConsecutiveCount = 0;  // Reset if voltage bounces
        }
        break;

      case BATTERY_CRITICAL:
        if (batteryVoltage < BATTERY_CUTOFF_THRESHOLD) {
          currentBatteryState = BATTERY_CUTOFF;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: CRITICAL → CUTOFF");
        } else if (batteryVoltage >= BATTERY_CRITICAL_HYSTERESIS) {
          // Hysteresis: need to rise above 3.3V to return to LOW
          currentBatteryState = BATTERY_LOW;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: CRITICAL → LOW (hysteresis)");
        }
        break;

      case BATTERY_CUTOFF:
        // Can only recover by charging above cutoff
        if (batteryVoltage >= BATTERY_CUTOFF_THRESHOLD + 0.2) {  // 200mV hysteresis
          currentBatteryState = BATTERY_CRITICAL;
          criticalConsecutiveCount = 0;
          DEBUG_PRINTLN("State: CUTOFF → CRITICAL");
        }
        break;
    }
}

void updateBatteryMonitor() {
  // Read battery voltage periodically
  if (millis() - lastBatteryReadTime >= BATTERY_READ_INTERVAL_MS) {
    float previousVoltage = lastBatteryVoltage;
    BatteryState previousState = currentBatteryState;

    lastBatteryVoltage = readBatteryVoltage();
    lastBatteryReadTime = millis();

    // Add BMS voltage drop to estimate actual battery terminal voltage
    float batteryVoltage = lastBatteryVoltage + BMS_VOLTAGE_DROP;

    // Update state machine
    updateBatteryStateMachine(batteryVoltage);

    // Display voltage changes or state transitions
    bool significantChange = abs(lastBatteryVoltage - previousVoltage) > 0.1;
    bool stateChanged = (currentBatteryState != previousState);
    bool timeToDisplay = (millis() - lastBatteryDisplayTime >= BATTERY_DISPLAY_INTERVAL_MS);

    if (significantChange || stateChanged || timeToDisplay) {
      DEBUG_PRINT("Battery: ");
      DEBUG_PRINT(batteryVoltage);
      DEBUG_PRINT("V (");

      switch (currentBatteryState) {
        case BATTERY_NORMAL:
          DEBUG_PRINT("NORMAL");
          break;
        case BATTERY_LOW:
          DEBUG_PRINT("LOW");
          break;
        case BATTERY_CRITICAL:
          DEBUG_PRINT("CRITICAL");
          break;
        case BATTERY_CUTOFF:
          DEBUG_PRINT("CUTOFF");
          break;
      }

      DEBUG_PRINTLN(")");
      lastBatteryDisplayTime = millis();
    }
  }
}

void displayBatteryStatus() {
  float batteryVoltage = lastBatteryVoltage + BMS_VOLTAGE_DROP;

  DEBUG_PRINT("Battery: ");
  DEBUG_PRINT(batteryVoltage);
  DEBUG_PRINT("V (");

  switch (currentBatteryState) {
    case BATTERY_NORMAL:
      if (batteryVoltage >= BATTERY_FULL) {
        DEBUG_PRINT("Full");
      } else if (batteryVoltage >= BATTERY_NOMINAL) {
        DEBUG_PRINT("Good");
      } else {
        DEBUG_PRINT("Normal");
      }
      break;
    case BATTERY_LOW:
      DEBUG_PRINT("LOW - please charge");
      break;
    case BATTERY_CRITICAL:
      DEBUG_PRINT("CRITICAL - charge now!");
      break;
    case BATTERY_CUTOFF:
      DEBUG_PRINT("CUTOFF - too low!");
      break;
  }

  DEBUG_PRINTLN(")");
}
