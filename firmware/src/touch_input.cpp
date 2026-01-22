#include "touch_input.h"
#include "led_control.h"  // For brightness and brightnessDirection globals

// Button state
static bool lastState = false;
static bool currentState = false;
static unsigned long pressStartTime = 0;
static bool longPressFired = false;
static unsigned long lastBrightnessChange = 0;
static bool brightnessStepMode = false;

// Callback function pointers
static void (*onTap)() = nullptr;
static void (*onLongPress)() = nullptr;

void initTouch() {
  pinMode(TOUCH_PIN, INPUT);
  DEBUG_PRINTLN("Touch input initialized");
}

void setTapCallback(void (*callback)()) {
  onTap = callback;
}

void setLongPressCallback(void (*callback)()) {
  onLongPress = callback;
}

void updateButton() {
  bool reading = digitalRead(TOUCH_PIN) == HIGH;

  // Debounce: only accept state change if stable
  static unsigned long lastChangeTime = 0;

  if (reading != lastState) {
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
        brightnessStepMode = false;
        DEBUG_PRINTLN("Press started");
      } else {
        // Button just released
        if (!longPressFired) {
          // It was a tap (released before long press threshold)
          if (onTap) onTap();
        } else {
          // Released after long press - reverse direction if at boundary
          if (brightness == 1 || brightness == MAX_BRIGHTNESS) {
            brightnessDirection *= -1;  // Reverse for next hold
            DEBUG_PRINTLN("At boundary - direction reversed for next hold");
          }
        }
        brightnessStepMode = false;
        DEBUG_PRINTLN("Released");
      }
    }

    // Check for long press while held
    if (currentState && !longPressFired) {
      if ((millis() - pressStartTime) >= LONG_PRESS_MS) {
        longPressFired = true;
        brightnessStepMode = true;
        lastBrightnessChange = millis();
        if (onLongPress) onLongPress();
      }
    }

    // Continuously increment brightness while held (stops automatically at boundaries)
    if (currentState && brightnessStepMode) {
      if ((millis() - lastBrightnessChange) >= BRIGHTNESS_INCREMENT_MS) {
        lastBrightnessChange = millis();
        if (onLongPress) onLongPress();
      }
    }
  }

  lastState = reading;
}
