#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "config.h"
#include "led_control.h"
#include "touch_input.h"
#include "battery_monitor.h"

// Deep sleep timeout tracking
unsigned long offStateStartTime = 0;
bool justWokeFromSleep = false;  // Track if we just woke from deep sleep

// Forward declarations
void enterDeepSleep();

// Callback: Handle tap events
void handleTap() {
  DEBUG_PRINTLN(">>> TAP detected!");
  offStateStartTime = 0;  // Reset sleep timer on any interaction

  // Read battery state immediately (fresh reading)
  readBatteryVoltage();
  BatteryState batteryState = getBatteryState();

  // CUTOFF: Refuse to turn on, go to deep sleep
  if (batteryState == BATTERY_CUTOFF) {
    DEBUG_PRINTLN("Battery CUTOFF - refusing to turn on");
    currentLampState = OFF;
    updateLED();
    delay(1000);  // Brief delay to show the debug message
    enterDeepSleep();
    return;  // Won't reach here, but for clarity
  }

  // Advance state normally
  advanceState();
  updateLED();

  // Display battery status on mode change
  displayBatteryStatus();

  // Flash warning based on battery state
  if (currentLampState != OFF) {
    if (batteryState == BATTERY_CRITICAL) {
      // CRITICAL: Flash on every mode change
      flashLowBatteryWarning();
    } else if (batteryState == BATTERY_LOW && justWokeFromSleep) {
      // LOW: Flash only when waking from deep sleep
      flashLowBatteryWarning();
      justWokeFromSleep = false;  // Clear flag after first tap
    }
  }
}

// Callback: Handle long press / continuous brightness adjustment
void handleLongPress() {
  offStateStartTime = 0;  // Reset sleep timer on any interaction
  incrementBrightness();
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Brief delay for USB CDC to enumerate

  // Disable WiFi and Bluetooth to save power
  WiFi.mode(WIFI_OFF);
  btStop();
  DEBUG_PRINTLN("WiFi and Bluetooth disabled");

  // Check if we woke from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Initialize modules
  initTouch();
  initLED();
  initBatteryMonitor();

  // Register callbacks
  setTapCallback(handleTap);
  setLongPressCallback(handleLongPress);

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    // Woke from deep sleep via touch - go directly to WHITE state
    DEBUG_PRINTLN("Woke from deep sleep!");

    // Disable GPIO hold to allow PWM control again
    gpio_hold_dis((gpio_num_t)WHITE_LED_PIN);
    gpio_hold_dis((gpio_num_t)WARM_LED_PIN);

    justWokeFromSleep = true;  // Set flag for LOW battery warning behavior

    currentLampState = WHITE;
    updateLED();
  } else {
    DEBUG_PRINTLN("Power-on or reset");
    justWokeFromSleep = false;  // Not a wake from sleep
  }

  DEBUG_PRINTLN("Lamp ready");
  DEBUG_PRINTLN("Tap: Cycle WHITE -> WARM -> OFF (smooth transitions)");
  DEBUG_PRINTLN("Hold: Continuous brightness adjustment (gamma corrected)");
  DEBUG_PRINT("Deep sleep after ");
  DEBUG_PRINT(DEEP_SLEEP_TIMEOUT_MS / 1000);
  DEBUG_PRINTLN("s in OFF state");
}

void loop() {
  updateButton();
  updateModeTransition();  // Smooth crossfade between modes (OFF/WHITE/WARM)
  updateBatteryMonitor();  // Periodic battery voltage monitoring

  // Track time in OFF state for deep sleep
  if (currentLampState == OFF) {
    if (offStateStartTime == 0) {
      offStateStartTime = millis();
      DEBUG_PRINTLN("OFF state - deep sleep timer started");
    } else if (millis() - offStateStartTime >= DEEP_SLEEP_TIMEOUT_MS) {
      enterDeepSleep();
    }
  } else {
    offStateStartTime = 0;  // Reset timer when not in OFF state
  }

  delay(1);  // Minimal delay for smooth mode transitions
}

void enterDeepSleep() {
  DEBUG_PRINTLN("Entering deep sleep...");
  DEBUG_PRINTLN("Touch button to wake");

  // Ensure LEDs are completely off using LEDC (not analogWrite)
  ledcWrite(0, 0);  // WHITE_LED_CHANNEL
  ledcWrite(1, 0);  // WARM_LED_CHANNEL

  // CRITICAL: Set GPIO pins to OUTPUT LOW and HOLD them during deep sleep
  // This prevents GPIO leakage from partially turning on MOSFETs
  pinMode(WHITE_LED_PIN, OUTPUT);
  digitalWrite(WHITE_LED_PIN, LOW);
  gpio_hold_en((gpio_num_t)WHITE_LED_PIN);  // Hold GPIO10 LOW during sleep

  pinMode(WARM_LED_PIN, OUTPUT);
  digitalWrite(WARM_LED_PIN, LOW);
  gpio_hold_en((gpio_num_t)WARM_LED_PIN);   // Hold GPIO5 LOW during sleep

  // Configure wake on GPIO3 (TOUCH_PIN) going HIGH
  // ESP32-C3 uses gpio_wakeup for deep sleep wake
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);

  delay(100);  // Allow serial to flush

  // Enter deep sleep (ESP32 will reset on wake)
  esp_deep_sleep_start();
}
