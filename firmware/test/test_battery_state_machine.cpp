#include <unity.h>

// For native testing, include Arduino mock first then provide implementations
#ifdef NATIVE
#include <math.h>
#include "Arduino.h"  // Mock from test/ folder

// Provide implementations for functions declared in Arduino.h
unsigned long millis() { return 0; }
int analogRead(uint8_t pin) { return 0; }
void delay(unsigned long ms) {}
void analogReadResolution(uint8_t bits) {}
void analogSetAttenuation(uint8_t attenuation) {}

SerialClass Serial;

#endif  // NATIVE

#include "config.h"

// Include battery monitor implementation directly for testing
#include "../src/battery_monitor.cpp"

// Mock voltage for testing
void setBatteryVoltage(float voltage) {
  lastBatteryVoltage = voltage - BMS_VOLTAGE_DROP;  // Subtract drop since updateBatteryMonitor adds it back
}

void setUp(void) {
  // Reset state before each test
  currentBatteryState = BATTERY_NORMAL;
  criticalConsecutiveCount = 0;
  lastBatteryVoltage = 3.7;  // Start at nominal voltage
}

void tearDown(void) {
  // Clean up after each test
}

// Test: Normal to LOW transition
void test_normal_to_low_transition(void) {
  currentBatteryState = BATTERY_NORMAL;

  // Set voltage to 3.4V (below 3.5V threshold)
  setBatteryVoltage(3.4);
  updateBatteryStateMachine(3.4);

  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
}

// Test: LOW to NORMAL transition
void test_low_to_normal_transition(void) {
  currentBatteryState = BATTERY_LOW;

  // Set voltage to 3.6V (above 3.5V threshold)
  setBatteryVoltage(3.6);
  updateBatteryStateMachine(3.6);

  TEST_ASSERT_EQUAL(BATTERY_NORMAL, currentBatteryState);
}

// Test: LOW to CRITICAL with hysteresis (requires 3 consecutive readings)
void test_low_to_critical_hysteresis(void) {
  currentBatteryState = BATTERY_LOW;
  criticalConsecutiveCount = 0;

  // First reading < 3.2V - should not transition yet
  setBatteryVoltage(3.1);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
  TEST_ASSERT_EQUAL(1, criticalConsecutiveCount);

  // Second reading < 3.2V - still not transitioned
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
  TEST_ASSERT_EQUAL(2, criticalConsecutiveCount);

  // Third reading < 3.2V - should transition now
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_CRITICAL, currentBatteryState);
}

// Test: Hysteresis counter resets if voltage bounces back
void test_critical_hysteresis_reset_on_bounce(void) {
  currentBatteryState = BATTERY_LOW;
  criticalConsecutiveCount = 0;

  // Two readings < 3.2V
  setBatteryVoltage(3.1);
  updateBatteryStateMachine(3.1);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(2, criticalConsecutiveCount);

  // Voltage bounces back above 3.2V - counter should reset
  setBatteryVoltage(3.3);
  updateBatteryStateMachine(3.3);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
  TEST_ASSERT_EQUAL(0, criticalConsecutiveCount);
}

// Test: CRITICAL to LOW transition (requires voltage > 3.3V for hysteresis)
void test_critical_to_low_hysteresis(void) {
  currentBatteryState = BATTERY_CRITICAL;

  // Voltage at 3.25V - should stay CRITICAL
  setBatteryVoltage(3.25);
  updateBatteryStateMachine(3.25);
  TEST_ASSERT_EQUAL(BATTERY_CRITICAL, currentBatteryState);

  // Voltage rises to 3.35V (above 3.3V hysteresis) - should transition to LOW
  setBatteryVoltage(3.35);
  updateBatteryStateMachine(3.35);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
}

// Test: Any state to CUTOFF (immediate transition)
void test_any_state_to_cutoff(void) {
  // From NORMAL
  currentBatteryState = BATTERY_NORMAL;
  setBatteryVoltage(2.9);
  updateBatteryStateMachine(2.9);
  TEST_ASSERT_EQUAL(BATTERY_CUTOFF, currentBatteryState);

  // Reset and test from LOW
  setUp();
  currentBatteryState = BATTERY_LOW;
  setBatteryVoltage(2.9);
  updateBatteryStateMachine(2.9);
  TEST_ASSERT_EQUAL(BATTERY_CUTOFF, currentBatteryState);

  // Reset and test from CRITICAL
  setUp();
  currentBatteryState = BATTERY_CRITICAL;
  setBatteryVoltage(2.9);
  updateBatteryStateMachine(2.9);
  TEST_ASSERT_EQUAL(BATTERY_CUTOFF, currentBatteryState);
}

// Test: CUTOFF to CRITICAL recovery (requires voltage > 3.2V for hysteresis)
void test_cutoff_to_critical_recovery(void) {
  currentBatteryState = BATTERY_CUTOFF;

  // Voltage at 3.1V - should stay CUTOFF
  setBatteryVoltage(3.1);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_CUTOFF, currentBatteryState);

  // Voltage rises to 3.25V (above 3.0V + 0.2V hysteresis) - should transition to CRITICAL
  setBatteryVoltage(3.25);
  updateBatteryStateMachine(3.25);
  TEST_ASSERT_EQUAL(BATTERY_CRITICAL, currentBatteryState);
}

// Test: Battery-limited max brightness
void test_battery_limited_brightness(void) {
  // NORMAL state - should return MAX_BRIGHTNESS (from config.h)
  currentBatteryState = BATTERY_NORMAL;
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, getBatteryLimitedMaxBrightness());

  // CRITICAL state - should return limited brightness (50% of MAX_BRIGHTNESS)
  currentBatteryState = BATTERY_CRITICAL;
  TEST_ASSERT_EQUAL(CRITICAL_MAX_BRIGHTNESS, getBatteryLimitedMaxBrightness());

  // LOW state - should return MAX_BRIGHTNESS
  currentBatteryState = BATTERY_LOW;
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, getBatteryLimitedMaxBrightness());

  // CUTOFF state - doesn't matter, lamp won't turn on, but should return MAX
  currentBatteryState = BATTERY_CUTOFF;
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, getBatteryLimitedMaxBrightness());
}

// Test: Full discharge scenario
void test_full_discharge_scenario(void) {
  currentBatteryState = BATTERY_NORMAL;
  criticalConsecutiveCount = 0;

  // Start at 3.7V (NORMAL)
  setBatteryVoltage(3.7);
  updateBatteryStateMachine(3.7);
  TEST_ASSERT_EQUAL(BATTERY_NORMAL, currentBatteryState);

  // Drop to 3.4V (LOW)
  setBatteryVoltage(3.4);
  updateBatteryStateMachine(3.4);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);

  // Drop to 3.1V - requires 3 readings to go CRITICAL
  setBatteryVoltage(3.1);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_LOW, currentBatteryState);
  updateBatteryStateMachine(3.1);
  TEST_ASSERT_EQUAL(BATTERY_CRITICAL, currentBatteryState);

  // Drop to 2.9V (CUTOFF) - immediate
  setBatteryVoltage(2.9);
  updateBatteryStateMachine(2.9);
  TEST_ASSERT_EQUAL(BATTERY_CUTOFF, currentBatteryState);
}

#ifdef NATIVE
// Native test - use main() instead of setup()/loop()
int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_normal_to_low_transition);
  RUN_TEST(test_low_to_normal_transition);
  RUN_TEST(test_low_to_critical_hysteresis);
  RUN_TEST(test_critical_hysteresis_reset_on_bounce);
  RUN_TEST(test_critical_to_low_hysteresis);
  RUN_TEST(test_any_state_to_cutoff);
  RUN_TEST(test_cutoff_to_critical_recovery);
  RUN_TEST(test_battery_limited_brightness);
  RUN_TEST(test_full_discharge_scenario);

  return UNITY_END();
}
#else
// ESP32 test - use setup()/loop()
void setup() {
  UNITY_BEGIN();

  RUN_TEST(test_normal_to_low_transition);
  RUN_TEST(test_low_to_normal_transition);
  RUN_TEST(test_low_to_critical_hysteresis);
  RUN_TEST(test_critical_hysteresis_reset_on_bounce);
  RUN_TEST(test_critical_to_low_hysteresis);
  RUN_TEST(test_any_state_to_cutoff);
  RUN_TEST(test_cutoff_to_critical_recovery);
  RUN_TEST(test_battery_limited_brightness);
  RUN_TEST(test_full_discharge_scenario);

  UNITY_END();
}

void loop() {
  // Nothing here - tests run once in setup()
}
#endif
