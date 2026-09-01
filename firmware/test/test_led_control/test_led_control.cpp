#include <unity.h>

// For native testing, include Arduino mock first then provide implementations
#ifdef NATIVE
#include <math.h>
#include "Arduino.h"  // Mock from test/ folder (found via -Itest build flag)

// Controllable fake clock — advanced explicitly per test instead of relying on real
// wall-clock time, so the (time-based) brightness slew can be driven deterministically.
static unsigned long g_fakeMillis = 0;
unsigned long millis() { return g_fakeMillis; }

int analogRead(uint8_t pin) { return 0; }
int digitalRead(uint8_t pin) { return 0; }
void digitalWrite(uint8_t pin, uint8_t val) {}
void delay(unsigned long ms) {}
void pinMode(uint8_t pin, uint8_t mode) {}
void analogReadResolution(uint8_t bits) {}
void analogSetAttenuation(uint8_t attenuation) {}

// Records the last duty value written per LEDC channel, so tests can observe the
// "effective PWM" led_control.cpp actually applied — not just the internal brightness value.
static uint32_t g_ledcDuty[2] = {0, 0};
// Counts ledcDetachPin() calls, so a ramp-down can be asserted to have actually released the
// PWM peripheral at the end (not just written a duty of 0 and left the pins attached).
static int g_detachCount = 0;
void ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolutionBits) {}
void ledcAttachPin(uint8_t pin, uint8_t channel) {}
void ledcDetachPin(uint8_t pin) { g_detachCount++; }
void ledcWrite(uint8_t channel, uint32_t duty) {
  if (channel < 2) g_ledcDuty[channel] = duty;
}

SerialClass Serial;
#endif  // NATIVE

#include "config.h"

// Include both implementations directly. led_control.cpp calls into battery_monitor.cpp's
// getBatteryLimitedMaxBrightness() / getBrightnessCompensationFactor(); including both here
// (rather than linking) also gives this test direct access to led_control.cpp's `static`
// helpers (clampToBatteryLimit, getCompensatedPWM) and file-scope statics (brightness,
// currentLampState, slewInitialized, ...) the same way test_battery reaches
// battery_monitor.cpp's globals.
#include "../../src/battery_monitor.cpp"
#include "../../src/led_control.cpp"

// Mirrors test_battery's helper: sets the cached voltage battery_monitor.cpp reads from.
static void setBatteryVoltage(float voltage) {
  lastBatteryVoltage = voltage - BMS_VOLTAGE_DROP;
}

static void advanceMillis(unsigned long ms) {
  g_fakeMillis += ms;
}

void setUp(void) {
  g_fakeMillis = 0;
  g_ledcDuty[0] = 0;
  g_ledcDuty[1] = 0;
  g_detachCount = 0;
  currentBatteryState = BATTERY_NORMAL;
  criticalConsecutiveCount = 0;
  setBatteryVoltage(BRIGHTNESS_REFERENCE_VOLTAGE);  // factor = 1.0 — no compensation confound
  initLED();       // rebuilds the gamma LUT fresh; ledcSetup/ledcAttachPin/ledcWrite are no-ops
  currentLampState = OFF;
  slewInitialized = false;
}

void tearDown(void) {}

// --- clampToBatteryLimit ---

void test_clamp_normal_state_no_limit(void) {
  currentBatteryState = BATTERY_NORMAL;
  TEST_ASSERT_EQUAL(MAX_BRIGHTNESS, clampToBatteryLimit(MAX_BRIGHTNESS));
}

void test_clamp_low_state_caps_at_low_max(void) {
  currentBatteryState = BATTERY_LOW;
  TEST_ASSERT_EQUAL(LOW_MAX_BRIGHTNESS, clampToBatteryLimit(MAX_BRIGHTNESS));
}

void test_clamp_critical_state_caps_at_critical_max(void) {
  currentBatteryState = BATTERY_CRITICAL;
  TEST_ASSERT_EQUAL(CRITICAL_MAX_BRIGHTNESS, clampToBatteryLimit(MAX_BRIGHTNESS));
}

void test_clamp_passes_through_requests_already_under_the_limit(void) {
  // A ceiling, not a rescale: a request already below the limit is untouched.
  currentBatteryState = BATTERY_CRITICAL;
  uint16_t modestRequest = CRITICAL_MAX_BRIGHTNESS / 2;
  TEST_ASSERT_EQUAL(modestRequest, clampToBatteryLimit(modestRequest));
}

// --- End-to-end: pot mode's live tracking (updateBrightnessSlew) as voltage drops ---
//
// Simulates the pot held fully up (target = MAX_BRIGHTNESS, unchanged for the whole test)
// while battery state degrades underneath it — the "mechanical stop" scenario: the
// requested target never moves, only the applied brightness and the actual PWM duty
// written to the LED should.

void test_pot_tracking_reaches_full_brightness_when_normal(void) {
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);

  // One big jump (~10 time constants) is enough for the exponential ease to fully settle
  // (5 time constants only reaches ~99.3% of the way there, too loose for a tight WITHIN).
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  TEST_ASSERT_UINT16_WITHIN(5, MAX_BRIGHTNESS, brightness);
  TEST_ASSERT_EQUAL(getCompensatedPWM(brightness), g_ledcDuty[WARM_LED_CHANNEL]);
}

void test_pot_tracking_caps_at_low_max_brightness(void) {
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();  // settles near MAX_BRIGHTNESS first, battery still NORMAL

  // Battery drops to LOW without the pot moving at all (target is still MAX_BRIGHTNESS).
  currentBatteryState = BATTERY_LOW;
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  // The internal slew was already tracking well above LOW_MAX_BRIGHTNESS before the state
  // changed, so the clamp bites immediately and exactly — not asymptotically.
  TEST_ASSERT_EQUAL(LOW_MAX_BRIGHTNESS, brightness);
  TEST_ASSERT_EQUAL(getCompensatedPWM(brightness), g_ledcDuty[WARM_LED_CHANNEL]);
}

void test_pot_tracking_caps_at_critical_max_brightness(void) {
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  currentBatteryState = BATTERY_CRITICAL;
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  TEST_ASSERT_EQUAL(CRITICAL_MAX_BRIGHTNESS, brightness);
  TEST_ASSERT_EQUAL(getCompensatedPWM(brightness), g_ledcDuty[WARM_LED_CHANNEL]);
}

void test_pot_tracking_resumes_when_battery_recovers(void) {
  // Full discharge-and-recover sweep: NORMAL -> CRITICAL -> NORMAL, pot never moves.
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  currentBatteryState = BATTERY_CRITICAL;
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();
  TEST_ASSERT_EQUAL(CRITICAL_MAX_BRIGHTNESS, brightness);

  // Battery recovers; the pot was never touched (target is still MAX_BRIGHTNESS), so
  // brightness should climb back toward full, not stay stuck at the old ceiling — the
  // internal slew state kept easing toward the raw target the whole time underneath the
  // clamp, so this happens immediately, not with a fresh multi-second ease-in.
  currentBatteryState = BATTERY_NORMAL;
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  TEST_ASSERT_UINT16_WITHIN(5, MAX_BRIGHTNESS, brightness);
  TEST_ASSERT_EQUAL(getCompensatedPWM(brightness), g_ledcDuty[WARM_LED_CHANNEL]);
}

void test_pot_tracking_stays_below_limit_when_pot_already_lower(void) {
  // Pot is only requesting a quarter of full brightness — well under LOW's ~50% ceiling —
  // so LOW shouldn't change anything: this is a ceiling, not a blanket dim.
  uint16_t modestTarget = LOW_MAX_BRIGHTNESS / 2;
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(modestTarget);
  currentBatteryState = BATTERY_LOW;
  advanceMillis(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  updateBrightnessSlew();

  TEST_ASSERT_UINT16_WITHIN(5, modestTarget, brightness);
}


// --- turnOffSlewed(): the pot-mode ramp-down ---
//
// The bug these cover: turning the dial past POT_OFF_THRESHOLD used to call turnOff(), which
// abandons the slew and hands the fade to updateModeTransition()'s MODE_TRANSITION_MS linear
// crossfade. Because the pot's input filter settles ~30x faster than the output slew, the
// off threshold is typically crossed while the eased brightness is still near the top — so
// the lamp cut from near-full to black in 400 ms while the ramp *up* took seconds.

// Drives loop()'s cadence: many small slew ticks rather than one big jump, so these tests
// exercise the same path the firmware actually runs.
static void runSlewFor(unsigned long totalMs, unsigned long stepMs = 10) {
  for (unsigned long elapsed = 0; elapsed < totalMs; elapsed += stepMs) {
    advanceMillis(stepMs);
    updateBrightnessSlew();
  }
}

// Brings the lamp up to a settled full-brightness ON state, the way pot mode does.
static void bringUpAtFullBrightness(void) {
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);
  runSlewFor(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  TEST_ASSERT_UINT16_WITHIN(5, MAX_BRIGHTNESS, brightness);
}

void test_slewed_off_reports_off_immediately_but_keeps_the_light_on(void) {
  bringUpAtFullBrightness();

  turnOffSlewed();

  // The lamp is logically OFF the instant the dial crosses the threshold — main.cpp's OFF
  // edge, auto-off and deep-sleep timer all key off this and must not be delayed by the ramp.
  TEST_ASSERT_EQUAL(OFF, currentLampState);
  TEST_ASSERT_TRUE(isFadingToOff());
  // ...but the light itself is still on, and still near where it was.
  TEST_ASSERT_UINT16_WITHIN(5, MAX_BRIGHTNESS, brightness);
  TEST_ASSERT_GREATER_THAN_UINT32(0, g_ledcDuty[WARM_LED_CHANNEL]);
}

void test_slewed_off_still_lit_well_past_the_old_crossfade_duration(void) {
  bringUpAtFullBrightness();
  turnOffSlewed();

  // This is the regression: at MODE_TRANSITION_MS the old path was already fully dark.
  runSlewFor(MODE_TRANSITION_MS);

  TEST_ASSERT_TRUE(isFadingToOff());
  TEST_ASSERT_GREATER_THAN_UINT32(0, g_ledcDuty[WARM_LED_CHANNEL]);
  // And it has genuinely dimmed on the way — it is a ramp, not a hold-then-drop.
  TEST_ASSERT_LESS_THAN_UINT16(MAX_BRIGHTNESS, brightness);
}

void test_slewed_off_decays_monotonically_then_lands_at_zero(void) {
  bringUpAtFullBrightness();
  turnOffSlewed();

  uint32_t previousDuty = g_ledcDuty[WARM_LED_CHANNEL];
  bool reachedZero = false;
  for (int i = 0; i < 2000; i++) {   // 20 s of 10 ms ticks — far more headroom than needed
    advanceMillis(10);
    updateBrightnessSlew();
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(previousDuty, g_ledcDuty[WARM_LED_CHANNEL]);
    previousDuty = g_ledcDuty[WARM_LED_CHANNEL];
    if (!isFadingToOff()) { reachedZero = true; break; }
  }

  TEST_ASSERT_TRUE_MESSAGE(reachedZero, "ramp-down never terminated");
  TEST_ASSERT_EQUAL(0, brightness);
  TEST_ASSERT_EQUAL_UINT32(0, g_ledcDuty[WARM_LED_CHANNEL]);
  TEST_ASSERT_EQUAL_UINT32(0, g_ledcDuty[WHITE_LED_CHANNEL]);
  // Both pins released, matching what updateModeTransition() does at the end of a fade to OFF.
  TEST_ASSERT_EQUAL_INT(2, g_detachCount);
}

void test_slewed_off_is_slower_than_the_crossfade_it_replaced(void) {
  bringUpAtFullBrightness();
  turnOffSlewed();

  unsigned long elapsed = 0;
  while (isFadingToOff() && elapsed < 20000) {
    advanceMillis(10);
    updateBrightnessSlew();
    elapsed += 10;
  }

  TEST_ASSERT_FALSE(isFadingToOff());
  // The whole point: the ramp down is on the slew's timescale, not the crossfade's.
  TEST_ASSERT_GREATER_THAN_UINT32(MODE_TRANSITION_MS, elapsed);
  TEST_ASSERT_GREATER_THAN_UINT32(BRIGHTNESS_SLEW_TIME_CONSTANT_MS, elapsed);
}

void test_ramp_down_interrupted_by_the_pot_coming_back_up_resumes_smoothly(void) {
  bringUpAtFullBrightness();
  turnOffSlewed();

  runSlewFor(BRIGHTNESS_SLEW_TIME_CONSTANT_MS);   // Part-way down
  TEST_ASSERT_TRUE(isFadingToOff());
  uint16_t brightnessAtInterrupt = brightness;
  TEST_ASSERT_GREATER_THAN_UINT16(0, brightnessAtInterrupt);

  // Pot comes back up: main.cpp's turn-on path for pot mode.
  turnOnAtZero(MODE_WARM);
  setBrightnessTarget(MAX_BRIGHTNESS);

  TEST_ASSERT_EQUAL(ON, currentLampState);
  TEST_ASSERT_FALSE(isFadingToOff());
  // Crucially it does NOT snap to black and re-ramp from zero — it picks up where it was.
  TEST_ASSERT_EQUAL_UINT16(brightnessAtInterrupt, brightness);

  runSlewFor(10 * BRIGHTNESS_SLEW_TIME_CONSTANT_MS);
  TEST_ASSERT_UINT16_WITHIN(5, MAX_BRIGHTNESS, brightness);
}

void test_plain_turn_off_still_uses_the_fast_crossfade(void) {
  // Button mode's path is deliberately unchanged.
  bringUpAtFullBrightness();

  turnOff();
  TEST_ASSERT_EQUAL(OFF, currentLampState);
  TEST_ASSERT_FALSE(isFadingToOff());

  advanceMillis(MODE_TRANSITION_MS);
  updateModeTransition();
  TEST_ASSERT_EQUAL_UINT32(0, g_ledcDuty[WARM_LED_CHANNEL]);
}

// --- Slew clock hygiene ---

void test_slew_does_not_jump_after_yielding_to_a_mode_crossfade(void) {
  bringUpAtFullBrightness();

  // Pot swept to the bottom of the ON range while a mode crossfade owns the output.
  setBrightnessTarget(1);
  swapMode(MODE_WARM, MAX_BRIGHTNESS);          // isTransitioning = true
  runSlewFor(MODE_TRANSITION_MS);               // Slew yields for the whole crossfade
  updateModeTransition();                       // ...which now completes
  advanceMillis(MODE_TRANSITION_MS);
  updateModeTransition();

  // First slew tick after the crossfade must see a ~1 tick dt, not the crossfade's whole
  // duration — otherwise alpha is near 1 and the brightness snaps to the target in one step.
  uint16_t before = brightness;
  advanceMillis(10);
  updateBrightnessSlew();
  TEST_ASSERT_LESS_THAN_UINT16(before / 2, before - brightness);
}

void test_ramp_down_cancels_a_battery_pulse_in_flight(void) {
  // The pot is polled regardless of the indicator, so the dial can cross the off threshold
  // mid-pulse. Both drive the same channel, so one of them has to yield.
  bringUpAtFullBrightness();
  playBatteryIndicator(BATTERY_LOW);
  TEST_ASSERT_TRUE(isPlayingIndicator());

  turnOffSlewed();
  TEST_ASSERT_FALSE(isPlayingIndicator());

  // ...and the ramp-down then runs to completion rather than stalling behind it.
  for (int i = 0; i < 2000 && isFadingToOff(); i++) {
    advanceMillis(10);
    updateBrightnessSlew();
  }
  TEST_ASSERT_FALSE(isFadingToOff());
  TEST_ASSERT_EQUAL_UINT32(0, g_ledcDuty[WARM_LED_CHANNEL]);
}

#ifdef NATIVE
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_normal_state_no_limit);
  RUN_TEST(test_clamp_low_state_caps_at_low_max);
  RUN_TEST(test_clamp_critical_state_caps_at_critical_max);
  RUN_TEST(test_clamp_passes_through_requests_already_under_the_limit);
  RUN_TEST(test_pot_tracking_reaches_full_brightness_when_normal);
  RUN_TEST(test_pot_tracking_caps_at_low_max_brightness);
  RUN_TEST(test_pot_tracking_caps_at_critical_max_brightness);
  RUN_TEST(test_pot_tracking_resumes_when_battery_recovers);
  RUN_TEST(test_pot_tracking_stays_below_limit_when_pot_already_lower);
  RUN_TEST(test_slewed_off_reports_off_immediately_but_keeps_the_light_on);
  RUN_TEST(test_slewed_off_still_lit_well_past_the_old_crossfade_duration);
  RUN_TEST(test_slewed_off_decays_monotonically_then_lands_at_zero);
  RUN_TEST(test_slewed_off_is_slower_than_the_crossfade_it_replaced);
  RUN_TEST(test_ramp_down_interrupted_by_the_pot_coming_back_up_resumes_smoothly);
  RUN_TEST(test_plain_turn_off_still_uses_the_fast_crossfade);
  RUN_TEST(test_slew_does_not_jump_after_yielding_to_a_mode_crossfade);
  RUN_TEST(test_ramp_down_cancels_a_battery_pulse_in_flight);
  return UNITY_END();
}
#else
void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_normal_state_no_limit);
  RUN_TEST(test_clamp_low_state_caps_at_low_max);
  RUN_TEST(test_clamp_critical_state_caps_at_critical_max);
  RUN_TEST(test_clamp_passes_through_requests_already_under_the_limit);
  RUN_TEST(test_pot_tracking_reaches_full_brightness_when_normal);
  RUN_TEST(test_pot_tracking_caps_at_low_max_brightness);
  RUN_TEST(test_pot_tracking_caps_at_critical_max_brightness);
  RUN_TEST(test_pot_tracking_resumes_when_battery_recovers);
  RUN_TEST(test_pot_tracking_stays_below_limit_when_pot_already_lower);
  RUN_TEST(test_slewed_off_reports_off_immediately_but_keeps_the_light_on);
  RUN_TEST(test_slewed_off_still_lit_well_past_the_old_crossfade_duration);
  RUN_TEST(test_slewed_off_decays_monotonically_then_lands_at_zero);
  RUN_TEST(test_slewed_off_is_slower_than_the_crossfade_it_replaced);
  RUN_TEST(test_ramp_down_interrupted_by_the_pot_coming_back_up_resumes_smoothly);
  RUN_TEST(test_plain_turn_off_still_uses_the_fast_crossfade);
  RUN_TEST(test_slew_does_not_jump_after_yielding_to_a_mode_crossfade);
  RUN_TEST(test_ramp_down_cancels_a_battery_pulse_in_flight);
  UNITY_END();
}

void loop() {}
#endif
