#include <unity.h>

#ifdef NATIVE
#include <math.h>
#include "Arduino.h"  // Mock from test/ folder (found via -Itest build flag)

// Controllable clock for time-based logic
static unsigned long mockMillisValue = 0;
unsigned long millis() { return mockMillisValue; }
void delay(unsigned long ms) { mockMillisValue += ms; }

// Controllable touch pin state
static bool mockTouchPin = false;
int digitalRead(uint8_t pin) { return mockTouchPin ? HIGH : LOW; }
void pinMode(uint8_t pin, uint8_t mode) {}

SerialClass Serial;
#endif  // NATIVE

#include "config.h"
#include "../../src/touch_input.cpp"

// Callback counters
static int singleTapFired     = 0;
static int doubleTapFired     = 0;
static int tripleTapFired     = 0;
static int longPressStartFired = 0;
static int longPressHoldFired  = 0;
static int longPressEndFired   = 0;

void onSingleTapCB()      { singleTapFired++; }
void onDoubleTapCB()      { doubleTapFired++; }
void onTripleTapCB()      { tripleTapFired++; }
void onLongPressStartCB() { longPressStartFired++; }
void onLongPressHoldCB()  { longPressHoldFired++; }
void onLongPressEndCB()   { longPressEndFired++; }

// Advance simulated time, calling updateButton() each millisecond
static void advanceTime(unsigned long ms) {
  unsigned long target = mockMillisValue + ms;
  while (mockMillisValue < target) {
    mockMillisValue++;
    updateButton();
  }
}

// Simulate a stable press (debounced).
// The debounce check is > DEBOUNCE_MS, so elapsed must be >= DEBOUNCE_MS+1.
// Pin changes on tick 1, so we need DEBOUNCE_MS+2 total ticks to get elapsed=DEBOUNCE_MS+1.
static void pressPin() {
  mockTouchPin = true;
  advanceTime(DEBOUNCE_MS + 2);
}

// Simulate a stable release (same timing as pressPin)
static void releasePin() {
  mockTouchPin = false;
  advanceTime(DEBOUNCE_MS + 2);
}

void setUp(void) {
  // Flush any pending gesture/long-press state from the previous test.
  // Advance far past all thresholds so all timers expire and state settles.
  mockTouchPin = false;
  mockMillisValue += LONG_PRESS_MS + GESTURE_WINDOW_MS + DEBOUNCE_MS * 3 + 10;
  for (int i = 0; i < (int)(DEBOUNCE_MS + GESTURE_WINDOW_MS + 2); i++) {
    mockMillisValue++;
    updateButton();
  }

  // Reset simulated time and counters for the new test
  mockMillisValue     = 0;
  singleTapFired      = 0;
  doubleTapFired      = 0;
  tripleTapFired      = 0;
  longPressStartFired = 0;
  longPressHoldFired  = 0;
  longPressEndFired   = 0;

  initTouch();
  setSingleTapCallback(onSingleTapCB);
  setDoubleTapCallback(onDoubleTapCB);
  setTripleTapCallback(onTripleTapCB);
  setLongPressStartCallback(onLongPressStartCB);
  setLongPressHoldCallback(onLongPressHoldCB);
  setLongPressEndCallback(onLongPressEndCB);
  setTouchBlocked(false);
}

void tearDown(void) {}

// --- Gesture detection ---

void test_single_tap(void) {
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);

  TEST_ASSERT_EQUAL(1, singleTapFired);
  TEST_ASSERT_EQUAL(0, doubleTapFired);
  TEST_ASSERT_EQUAL(0, tripleTapFired);
  TEST_ASSERT_EQUAL(0, longPressStartFired);
}

void test_double_tap(void) {
  pressPin();
  releasePin();
  advanceTime(DEBOUNCE_MS);   // Short gap within gesture window
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);

  TEST_ASSERT_EQUAL(0, singleTapFired);
  TEST_ASSERT_EQUAL(1, doubleTapFired);
  TEST_ASSERT_EQUAL(0, tripleTapFired);
}

void test_triple_tap(void) {
  pressPin();
  releasePin();
  advanceTime(DEBOUNCE_MS);
  pressPin();
  releasePin();
  advanceTime(DEBOUNCE_MS);
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);

  TEST_ASSERT_EQUAL(0, singleTapFired);
  TEST_ASSERT_EQUAL(0, doubleTapFired);
  TEST_ASSERT_EQUAL(1, tripleTapFired);
}

// Four or more taps are capped at triple
void test_four_taps_treated_as_triple(void) {
  for (int i = 0; i < 4; i++) {
    pressPin();
    releasePin();
    advanceTime(DEBOUNCE_MS);
  }
  advanceTime(GESTURE_WINDOW_MS);

  TEST_ASSERT_EQUAL(0, singleTapFired);
  TEST_ASSERT_EQUAL(0, doubleTapFired);
  TEST_ASSERT_EQUAL(1, tripleTapFired);
}

// Taps separated by more than GESTURE_WINDOW_MS are distinct single taps
void test_two_separated_taps_are_two_single_taps(void) {
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS + 10);
  TEST_ASSERT_EQUAL(1, singleTapFired);

  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS + 10);

  TEST_ASSERT_EQUAL(2, singleTapFired);
  TEST_ASSERT_EQUAL(0, doubleTapFired);
}

// --- Long press ---

void test_long_press_start(void) {
  pressPin();
  advanceTime(LONG_PRESS_MS);

  TEST_ASSERT_EQUAL(1, longPressStartFired);
  TEST_ASSERT_EQUAL(0, singleTapFired);
  TEST_ASSERT_EQUAL(0, longPressHoldFired);
}

void test_long_press_hold_fires_repeatedly(void) {
  pressPin();
  advanceTime(LONG_PRESS_MS);           // start fires once
  advanceTime(BRIGHTNESS_STEP_MS * 3);  // 3 hold callbacks

  TEST_ASSERT_EQUAL(1, longPressStartFired);
  TEST_ASSERT_EQUAL(3, longPressHoldFired);
}

void test_long_press_end_on_release(void) {
  pressPin();
  advanceTime(LONG_PRESS_MS);
  releasePin();

  TEST_ASSERT_EQUAL(1, longPressStartFired);
  TEST_ASSERT_EQUAL(1, longPressEndFired);
  TEST_ASSERT_EQUAL(0, singleTapFired);
}

// --- Touch blocking ---

void test_touch_blocked_prevents_tap_callbacks(void) {
  setTouchBlocked(true);
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);

  TEST_ASSERT_EQUAL(0, singleTapFired);
  TEST_ASSERT_EQUAL(0, longPressStartFired);
}

// The gesture timer runs even when blocked (flushes pending taps)
void test_gesture_timer_fires_when_blocked(void) {
  pressPin();
  releasePin();
  setTouchBlocked(true);           // Block before window expires
  advanceTime(GESTURE_WINDOW_MS);  // Timer runs despite blocking

  TEST_ASSERT_EQUAL(1, singleTapFired);
}

void test_unblock_restores_operation(void) {
  setTouchBlocked(true);
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);
  TEST_ASSERT_EQUAL(0, singleTapFired);  // Blocked - did not fire

  setTouchBlocked(false);
  pressPin();
  releasePin();
  advanceTime(GESTURE_WINDOW_MS);
  TEST_ASSERT_EQUAL(1, singleTapFired);  // Unblocked - fires
}

#ifdef NATIVE
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_single_tap);
  RUN_TEST(test_double_tap);
  RUN_TEST(test_triple_tap);
  RUN_TEST(test_four_taps_treated_as_triple);
  RUN_TEST(test_two_separated_taps_are_two_single_taps);
  RUN_TEST(test_long_press_start);
  RUN_TEST(test_long_press_hold_fires_repeatedly);
  RUN_TEST(test_long_press_end_on_release);
  RUN_TEST(test_touch_blocked_prevents_tap_callbacks);
  RUN_TEST(test_gesture_timer_fires_when_blocked);
  RUN_TEST(test_unblock_restores_operation);
  return UNITY_END();
}
#else
void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_single_tap);
  RUN_TEST(test_double_tap);
  RUN_TEST(test_triple_tap);
  RUN_TEST(test_four_taps_treated_as_triple);
  RUN_TEST(test_two_separated_taps_are_two_single_taps);
  RUN_TEST(test_long_press_start);
  RUN_TEST(test_long_press_hold_fires_repeatedly);
  RUN_TEST(test_long_press_end_on_release);
  RUN_TEST(test_touch_blocked_prevents_tap_callbacks);
  RUN_TEST(test_gesture_timer_fires_when_blocked);
  RUN_TEST(test_unblock_restores_operation);
  UNITY_END();
}

void loop() {}
#endif
