#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include "config.h"

// Function declarations
void initTouch();
void updateButton();
void setTapCallback(void (*callback)());
void setLongPressCallback(void (*callback)());

#endif // TOUCH_INPUT_H
