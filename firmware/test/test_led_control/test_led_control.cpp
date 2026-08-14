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
void ledcSetup(uint8_t channel, uint32_t freq, uint8_t resolutionBits) {}
void ledcAttachPin(uint8_t pin, uint8_t channel) {}
void ledcDetachPin(uint8_t pin) {}
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
  UNITY_END();
}

void loop() {}
#endif
