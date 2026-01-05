#include <Arduino.h>
#include "config.h"
#include "led_control.h"
#include "touch_input.h"

// Callback: Handle tap events
void handleTap() {
  DEBUG_PRINTLN(">>> TAP detected!");
  advanceState();
  updateLED();
}

// Callback: Handle long press / brightness stepping
void handleLongPress() {
  stepBrightness();
}

void setup() {
  Serial.begin(115200);

  // Initialize modules
  initTouch();
  initLED();

  // Register callbacks
  setTapCallback(handleTap);
  setLongPressCallback(handleLongPress);

  DEBUG_PRINTLN("Lamp ready");
  DEBUG_PRINTLN("Tap: Cycle WHITE -> WARM -> OFF");
  DEBUG_PRINTLN("Hold: Step through brightness levels (when ON)");
}

void loop() {
  updateButton();
  delay(10);  // Small delay to avoid hammering the CPU
}
