#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include "config.h"

// Input provider abstraction — allows swapping between a physical button, capacitive
// touch, or an event-based sensor without touching the gesture logic below.
// The reader function should return true when the input is "active" (pressed/detected).
// Default: reads BUTTON_PIN HIGH (physical button with external pull-down).
typedef bool (*InputStateReader)(void);
void registerInputReader(InputStateReader reader);

// For event-based sensors (e.g. accelerometer tap interrupt): call injectInputEvent(true)
// then injectInputEvent(false) after a short delay to simulate a press/release cycle.
// This also automatically registers the injected-state reader.
void injectInputEvent(bool pressed);

void initTouch();
void updateButton();

// Gesture callbacks
void setSingleTapCallback(void (*callback)());
void setDoubleTapCallback(void (*callback)());
void setTripleTapCallback(void (*callback)());

// Long press callbacks
void setLongPressStartCallback(void (*callback)());
void setLongPressHoldCallback(void (*callback)());   // Called every BRIGHTNESS_STEP_MS while held
void setLongPressEndCallback(void (*callback)());    // Called on release after long press

// Block/unblock touch input (during battery indicator animation)
void setTouchBlocked(bool blocked);

#endif // TOUCH_INPUT_H
