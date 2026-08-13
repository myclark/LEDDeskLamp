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

void test_map_within_top_deadzone_snaps_to_max(void) {
  // Close to full scale (well within POT_MAX_DEADZONE) should snap to exactly MAX_BRIGHTNESS
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, mapPotToBrightness(POT_ADC_MAX - 20));
}

void test_map_outside_top_deadzone_does_not_snap(void) {
  // Comfortably below the deadzone (80% of full scale) should not snap
  uint8_t mapped = mapPotToBrightness((POT_ADC_MAX * 80) / 100);
  TEST_ASSERT_LESS_THAN(MAX_BRIGHTNESS, mapped);
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

// --- smoothPotReading (tick-to-tick noise filter) ---

void test_smooth_no_time_elapsed_holds_current(void) {
  TEST_ASSERT_EQUAL_FLOAT(50.0f, smoothPotReading(50.0f, 200.0f, 0));
}

void test_smooth_long_dt_converges_to_target(void) {
  float result = smoothPotReading(0.0f, 200.0f, POT_FILTER_TIME_CONSTANT_MS * 20);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 200.0f, result);
}

void test_smooth_one_time_constant_is_partial_step(void) {
  // After one time constant, should be ~63.2% of the way to target (1 - 1/e) — not
  // instantly at the target, and not still at the start.
  float result = smoothPotReading(0.0f, 100.0f, POT_FILTER_TIME_CONSTANT_MS);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 63.2f, result);
}

void test_smooth_rejects_brief_noise_spike(void) {
  // A brief one-tick noise bump (realistic ADC jitter, not a full-scale outlier)
  // shouldn't move the filtered value anywhere near as far as the raw spike itself —
  // this is the point: absorbing single-sample noise at any dial position.
  float steady = 2000.0f;
  float spike = steady + 200.0f;  // one noisy sample, +200 raw ADC counts
  float afterSpike = smoothPotReading(steady, spike, 1);  // 1ms tick
  float movedBy = afterSpike - steady;
  TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, movedBy);
  TEST_ASSERT_LESS_THAN_FLOAT(20.0f, movedBy);
}

#ifdef NATIVE
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_map_zero_adc_to_zero_brightness);
  RUN_TEST(test_map_full_scale_adc_to_max_brightness);
  RUN_TEST(test_map_midscale_adc_to_roughly_half_brightness);
  RUN_TEST(test_map_clamps_out_of_range_low);
  RUN_TEST(test_map_clamps_out_of_range_high);
  RUN_TEST(test_map_within_top_deadzone_snaps_to_max);
  RUN_TEST(test_map_outside_top_deadzone_does_not_snap);
  RUN_TEST(test_stays_off_below_on_hysteresis);
  RUN_TEST(test_turns_on_at_on_hysteresis);
  RUN_TEST(test_stays_on_above_off_threshold);
  RUN_TEST(test_turns_off_at_off_threshold);
  RUN_TEST(test_dead_zone_holds_last_state_while_on);
  RUN_TEST(test_dead_zone_holds_last_state_while_off);
  RUN_TEST(test_full_off_to_on_to_off_sweep);
  RUN_TEST(test_smooth_no_time_elapsed_holds_current);
  RUN_TEST(test_smooth_long_dt_converges_to_target);
  RUN_TEST(test_smooth_one_time_constant_is_partial_step);
  RUN_TEST(test_smooth_rejects_brief_noise_spike);
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
  RUN_TEST(test_map_within_top_deadzone_snaps_to_max);
  RUN_TEST(test_map_outside_top_deadzone_does_not_snap);
  RUN_TEST(test_stays_off_below_on_hysteresis);
  RUN_TEST(test_turns_on_at_on_hysteresis);
  RUN_TEST(test_stays_on_above_off_threshold);
  RUN_TEST(test_turns_off_at_off_threshold);
  RUN_TEST(test_dead_zone_holds_last_state_while_on);
  RUN_TEST(test_dead_zone_holds_last_state_while_off);
  RUN_TEST(test_full_off_to_on_to_off_sweep);
  RUN_TEST(test_smooth_no_time_elapsed_holds_current);
  RUN_TEST(test_smooth_long_dt_converges_to_target);
  RUN_TEST(test_smooth_one_time_constant_is_partial_step);
  RUN_TEST(test_smooth_rejects_brief_noise_spike);
  UNITY_END();
}

void loop() {}
#endif
