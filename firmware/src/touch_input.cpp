#include "touch_input.h"

// --- Callback pointers ---
static void (*onSingleTap)() = nullptr;
static void (*onDoubleTap)() = nullptr;
static void (*onTripleTap)() = nullptr;
static void (*onLongPressStart)() = nullptr;
static void (*onLongPressHold)() = nullptr;
static void (*onLongPressEnd)() = nullptr;

// --- Debounce state ---
static bool lastReading = false;
static bool currentState = false;
static unsigned long lastChangeTime = 0;

// --- Gesture accumulator ---
static uint8_t tapCount = 0;
static unsigned long lastReleaseTime = 0;
static bool gestureTimerActive = false;

// --- Long press state ---
static unsigned long pressStartTime = 0;
static bool longPressFired = false;
static bool longPressHolding = false;
static unsigned long lastBrightnessStep = 0;

// --- Touch blocking ---
static bool touchBlocked = false;

static void fireGesture() {
  if (tapCount == 1) {
    if (onSingleTap) onSingleTap();
  } else if (tapCount == 2) {
    if (onDoubleTap) onDoubleTap();
  } else {
    if (onTripleTap) onTripleTap();
  }
  tapCount = 0;
  gestureTimerActive = false;
}

void initTouch() {
  pinMode(TOUCH_PIN, INPUT);
  DEBUG_PRINTLN("Touch input initialized");
}

void setSingleTapCallback(void (*callback)()) { onSingleTap = callback; }
void setDoubleTapCallback(void (*callback)()) { onDoubleTap = callback; }
void setTripleTapCallback(void (*callback)()) { onTripleTap = callback; }
void setLongPressStartCallback(void (*callback)()) { onLongPressStart = callback; }
void setLongPressHoldCallback(void (*callback)()) { onLongPressHold = callback; }
void setLongPressEndCallback(void (*callback)()) { onLongPressEnd = callback; }
void setTouchBlocked(bool blocked) { touchBlocked = blocked; }

void updateButton() {
  // 1. Gesture timer check (runs even when blocked, to flush pending gestures)
  if (gestureTimerActive && (millis() - lastReleaseTime >= GESTURE_WINDOW_MS)) {
    fireGesture();
  }

  // 2. If blocked, return early
  if (touchBlocked) return;

  // 3. Debounce: accept state change only after DEBOUNCE_MS of stability
  bool reading = digitalRead(TOUCH_PIN) == HIGH;

  if (reading != lastReading) {
    lastChangeTime = millis();
  }

  if ((millis() - lastChangeTime) > DEBOUNCE_MS) {
    // State has been stable long enough

    if (reading != currentState) {
      currentState = reading;

      if (currentState) {
        // Button just pressed
        pressStartTime = millis();
        longPressFired = false;
        longPressHolding = false;
        DEBUG_PRINTLN("Press started");
      } else {
        // Button just released
        if (longPressFired) {
          if (onLongPressEnd) onLongPressEnd();
          longPressHolding = false;
        } else {
          // It was a tap - add to gesture accumulator
          tapCount++;
          if (tapCount > 3) tapCount = 3;  // Cap at triple
          lastReleaseTime = millis();
          gestureTimerActive = true;
        }
        DEBUG_PRINTLN("Released");
      }
    }

    // Check for long press threshold while held
    if (currentState && !longPressFired) {
      if ((millis() - pressStartTime) >= LONG_PRESS_MS) {
        longPressFired = true;
        longPressHolding = true;
        // Discard accumulated taps (edge case: tap-then-hold)
        tapCount = 0;
        gestureTimerActive = false;
        lastBrightnessStep = millis();
        if (onLongPressStart) onLongPressStart();
      }
    }

    // Fire hold callback periodically while in long press mode
    if (currentState && longPressHolding) {
      if ((millis() - lastBrightnessStep) >= BRIGHTNESS_STEP_MS) {
        lastBrightnessStep = millis();
        if (onLongPressHold) onLongPressHold();
      }
    }
  }

  lastReading = reading;
}
