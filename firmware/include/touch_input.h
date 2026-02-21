#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include "config.h"

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
