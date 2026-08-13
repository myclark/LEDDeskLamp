#include <unity.h>

#ifdef NATIVE
#include "Arduino.h"  // Mock from test/ folder (found via -Itest build flag)

unsigned long millis() { return 0; }
int analogRead(uint8_t pin) { return 0; }
void delay(unsigned long ms) {}
void pinMode(uint8_t pin, uint8_t mode) {}
void analogReadResolution(uint8_t bits) {}
void analogSetAttenuation(uint8_t attenuation) {}

SerialClass Serial;
#endif  // NATIVE

#include "config.h"
#include "../../src/pot_input.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- mapPotToBrightness ---

void test_map_zero_adc_to_zero_brightness(void) {
  TEST_ASSERT_EQUAL(0, mapPotToBrightness(0));
}

void test_map_full_scale_adc_to_max_brightness(void) {
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, mapPotToBrightness(POT_ADC_MAX));
}

void test_map_midscale_adc_to_roughly_half_brightness(void) {
  uint8_t mapped = mapPotToBrightness(POT_ADC_MAX / 2);
  TEST_ASSERT_UINT8_WITHIN(2, MAX_BRIGHTNESS / 2, mapped);
}

void test_map_clamps_out_of_range_low(void) {
  TEST_ASSERT_EQUAL(0, mapPotToBrightness(-10));
}

void test_map_clamps_out_of_range_high(void) {
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, mapPotToBrightness(POT_ADC_MAX + 500));
}

// --- updatePotStateMachine (on/off hysteresis) ---

void test_stays_off_below_on_hysteresis(void) {
  TEST_ASSERT_FALSE(updatePotStateMachine(false, POT_ON_HYSTERESIS - 1));
}

void test_turns_on_at_on_hysteresis(void) {
  TEST_ASSERT_TRUE(updatePotStateMachine(false, POT_ON_HYSTERESIS));
}

void test_stays_on_above_off_threshold(void) {
  TEST_ASSERT_TRUE(updatePotStateMachine(true, POT_OFF_THRESHOLD + 1));
}

void test_turns_off_at_off_threshold(void) {
  TEST_ASSERT_FALSE(updatePotStateMachine(true, POT_OFF_THRESHOLD));
}

void test_dead_zone_holds_last_state_while_on(void) {
  uint8_t midDeadZone = (POT_OFF_THRESHOLD + POT_ON_HYSTERESIS) / 2;
  TEST_ASSERT_TRUE(updatePotStateMachine(true, midDeadZone));
}

void test_dead_zone_holds_last_state_while_off(void) {
  uint8_t midDeadZone = (POT_OFF_THRESHOLD + POT_ON_HYSTERESIS) / 2;
  TEST_ASSERT_FALSE(updatePotStateMachine(false, midDeadZone));
}

void test_full_off_to_on_to_off_sweep(void) {
  bool on = false;
  on = updatePotStateMachine(on, 0);                     // fully CCW
  TEST_ASSERT_FALSE(on);
  on = updatePotStateMachine(on, MAX_BRIGHTNESS);         // fully CW
  TEST_ASSERT_TRUE(on);
  on = updatePotStateMachine(on, POT_OFF_THRESHOLD + 1);  // back down, just above off
  TEST_ASSERT_TRUE(on);
  on = updatePotStateMachine(on, 0);                      // fully CCW again
  TEST_ASSERT_FALSE(on);
}

#ifdef NATIVE
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_map_zero_adc_to_zero_brightness);
  RUN_TEST(test_map_full_scale_adc_to_max_brightness);
  RUN_TEST(test_map_midscale_adc_to_roughly_half_brightness);
  RUN_TEST(test_map_clamps_out_of_range_low);
  RUN_TEST(test_map_clamps_out_of_range_high);
  RUN_TEST(test_stays_off_below_on_hysteresis);
  RUN_TEST(test_turns_on_at_on_hysteresis);
  RUN_TEST(test_stays_on_above_off_threshold);
  RUN_TEST(test_turns_off_at_off_threshold);
  RUN_TEST(test_dead_zone_holds_last_state_while_on);
  RUN_TEST(test_dead_zone_holds_last_state_while_off);
  RUN_TEST(test_full_off_to_on_to_off_sweep);
  return UNITY_END();
}
#else
void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_map_zero_adc_to_zero_brightness);
  RUN_TEST(test_map_full_scale_adc_to_max_brightness);
  RUN_TEST(test_map_midscale_adc_to_roughly_half_brightness);
  RUN_TEST(test_map_clamps_out_of_range_low);
  RUN_TEST(test_map_clamps_out_of_range_high);
  RUN_TEST(test_stays_off_below_on_hysteresis);
  RUN_TEST(test_turns_on_at_on_hysteresis);
  RUN_TEST(test_stays_on_above_off_threshold);
  RUN_TEST(test_turns_off_at_off_threshold);
  RUN_TEST(test_dead_zone_holds_last_state_while_on);
  RUN_TEST(test_dead_zone_holds_last_state_while_off);
  RUN_TEST(test_full_off_to_on_to_off_sweep);
  UNITY_END();
}

void loop() {}
#endif
