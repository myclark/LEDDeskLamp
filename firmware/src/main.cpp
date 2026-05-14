#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include "config.h"
#include "led_control.h"
#include "touch_input.h"
#include "battery_monitor.h"
#ifdef USE_ACCEL_INPUT
#include <Wire.h>
#include "accel_input.h"
#endif

// RTC-persistent variables (survive deep sleep, lost on battery disconnect)
RTC_DATA_ATTR uint8_t savedMode = MODE_WARM;
RTC_DATA_ATTR uint8_t warmBrightness = DEFAULT_BRIGHTNESS;
RTC_DATA_ATTR uint8_t coolBrightness = DEFAULT_BRIGHTNESS;
RTC_DATA_ATTR uint16_t bootCount = 0;

// Forward declarations
void enterDeepSleep();

// Tracks last user interaction for auto-off timeout
static unsigned long lastInteractionTime = 0;

// Helper: pointer to the brightness variable for a given mode
static uint8_t* getModeBrightness(uint8_t mode) {
  return (mode == MODE_WARM) ? &warmBrightness : &coolBrightness;
}

// Callback: single tap — toggle ON/OFF
void handleSingleTap() {
  DEBUG_PRINTLN(">>> SINGLE TAP");
  lastInteractionTime = millis();

  if (currentLampState == ON) {
    turnOff();
  } else {
    // Fresh battery reading before turning on
    readBatteryVoltage();
    BatteryState batteryState = getBatteryState();

    if (batteryState == BATTERY_CUTOFF) {
      DEBUG_PRINTLN("Battery CUTOFF - refusing to turn on, entering deep sleep");
      enterDeepSleep();
    }

    turnOn(savedMode, *getModeBrightness(savedMode));

    // Auto battery indicator when LOW or CRITICAL
    if (batteryState == BATTERY_LOW || batteryState == BATTERY_CRITICAL) {
      setTouchBlocked(true);
      playBatteryIndicator(batteryState);
    }
  }
}

// Callback: double tap — swap warm/cool (ignored when OFF)
void handleDoubleTap() {
  DEBUG_PRINTLN(">>> DOUBLE TAP");
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  savedMode = (savedMode == MODE_WARM) ? MODE_COOL : MODE_WARM;
  swapMode(savedMode, *getModeBrightness(savedMode));
}

// Callback: triple tap — show battery level indicator (ignored when OFF)
void handleTripleTap() {
  DEBUG_PRINTLN(">>> TRIPLE TAP");
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  readBatteryVoltage();
  setTouchBlocked(true);
  playBatteryIndicator(getBatteryState());
}

// Callback: long press initial trigger — first brightness increment
void handleLongPressStart() {
  DEBUG_PRINTLN(">>> LONG PRESS START");
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  incrementBrightness();
  *getModeBrightness(savedMode) = getActiveBrightness();
}

// Callback: long press hold — continuous brightness increments
void handleLongPressHold() {
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  incrementBrightness();
  *getModeBrightness(savedMode) = getActiveBrightness();
}

// Callback: long press release — reverse direction for next hold
void handleLongPressEnd() {
  DEBUG_PRINTLN(">>> LONG PRESS END");
  reverseBrightnessDirection();
}

#ifdef USE_ACCEL_INPUT
// Non-blocking state machine: waits LIS3DH_DOUBLE_TAP_WAIT_MS after INT1 fires,
// then reads CLICK_SRC to discriminate single vs double tap.
static void updateAccelInput() {
  enum AccelState { IDLE, WAITING };
  static AccelState state = IDLE;
  static unsigned long intFiredAt = 0;

  if (state == IDLE) {
    if (digitalRead(LIS3DH_INT_PIN) == HIGH) {
      DEBUG_PRINTLN("ACCEL: INT1 fired — waiting to discriminate tap");
      intFiredAt = millis();
      state = WAITING;
    }
  } else {
    if (millis() - intFiredAt >= LIS3DH_DOUBLE_TAP_WAIT_MS) {
      uint8_t src = accelReadClickSrc();
      DEBUG_PRINT("ACCEL: CLICK_SRC = 0x");
      DEBUG_PRINTLN(src);
      bool doubleTap = (src >> 5) & 0x01;
      if (doubleTap) {
        DEBUG_PRINTLN("ACCEL: double tap → swap mode");
        handleDoubleTap();
      } else {
        DEBUG_PRINTLN("ACCEL: single tap → toggle on/off");
        handleSingleTap();
      }
      state = IDLE;
    }
  }
}
#endif

void setup() {
#if DEBUG
  Serial.begin(115200);
  delay(USB_CDC_INIT_DELAY_MS);
#endif

  // Disable WiFi and Bluetooth to save power
  WiFi.mode(WIFI_OFF);
  btStop();
  DEBUG_PRINTLN("WiFi and Bluetooth disabled");

  bootCount++;
  DEBUG_PRINT("Boot count: ");
  DEBUG_PRINTLN(bootCount);

  // Check wakeup reason before module init
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // Initialize modules
  initTouch();
  initLED();
  initBatteryMonitor();

#ifdef USE_ACCEL_INPUT
  Wire.begin(LIS3DH_SDA_PIN, LIS3DH_SCL_PIN);
  accelInit();
#endif

  // Register callbacks
  setSingleTapCallback(handleSingleTap);
  setDoubleTapCallback(handleDoubleTap);
  setTripleTapCallback(handleTripleTap);
#ifndef USE_ACCEL_INPUT
  setLongPressStartCallback(handleLongPressStart);
  setLongPressHoldCallback(handleLongPressHold);
  setLongPressEndCallback(handleLongPressEnd);
#endif

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    DEBUG_PRINTLN("Woke from deep sleep!");
    lastInteractionTime = millis();

    // Disable GPIO hold to allow PWM control again
    gpio_hold_dis((gpio_num_t)WHITE_LED_PIN);
    gpio_hold_dis((gpio_num_t)WARM_LED_PIN);

#ifdef USE_ACCEL_INPUT
    // Clear latched INT1 — on wakeup any tap means "turn on", no discrimination needed
    uint8_t wakeSrc = accelReadClickSrc();
    DEBUG_PRINT("ACCEL: wakeup CLICK_SRC = 0x");
    DEBUG_PRINTLN(wakeSrc);
#endif

    // Restore saved mode and brightness
    turnOn(savedMode, *getModeBrightness(savedMode));

    // Auto battery indicator when LOW or CRITICAL on wake
    readBatteryVoltage();
    BatteryState batteryState = getBatteryState();
    if (batteryState == BATTERY_LOW || batteryState == BATTERY_CRITICAL) {
      setTouchBlocked(true);
      playBatteryIndicator(batteryState);
    }
  } else {
    DEBUG_PRINTLN("Power-on or reset (staying in OFF)");
    DEBUG_PRINT("Saved mode: ");
    DEBUG_PRINTLN(savedMode == MODE_WARM ? "WARM" : "COOL");
    DEBUG_PRINT("Warm brightness: ");
    DEBUG_PRINT(warmBrightness);
    DEBUG_PRINT(", Cool brightness: ");
    DEBUG_PRINTLN(coolBrightness);
  }

  DEBUG_PRINTLN("Lamp ready");
  DEBUG_PRINTLN("Single tap: toggle ON/OFF");
  DEBUG_PRINTLN("Double tap: swap WARM/COOL");
  DEBUG_PRINTLN("Long press: adjust brightness");
  DEBUG_PRINTLN("Triple tap: battery indicator");
  DEBUG_PRINT("Deep sleep after ");
  DEBUG_PRINT(DEEP_SLEEP_TIMEOUT_MS / 1000);
  DEBUG_PRINTLN("s in OFF state");
#if AUTO_OFF_ENABLED
  DEBUG_PRINT("Auto-off after ");
  DEBUG_PRINT(AUTO_OFF_TIMEOUT_MS / 60000);
  DEBUG_PRINTLN("min with no interaction");
#endif
}

void loop() {
#ifdef USE_ACCEL_INPUT
  updateAccelInput();
#else
  updateButton();
#endif
  updateModeTransition();
  updateBatteryMonitor();
  updateBatteryIndicator();

  // Unblock touch when indicator animation finishes
  static bool indicatorWasPlaying = false;
  bool indicatorNowPlaying = isPlayingIndicator();
  if (indicatorWasPlaying && !indicatorNowPlaying) {
    setTouchBlocked(false);
  }
  indicatorWasPlaying = indicatorNowPlaying;

  // Auto-off: turn off after AUTO_OFF_TIMEOUT_MS of no user interaction
#if AUTO_OFF_ENABLED
  if (currentLampState == ON && !isPlayingIndicator()) {
    if (millis() - lastInteractionTime >= AUTO_OFF_TIMEOUT_MS) {
      DEBUG_PRINTLN("Auto-off: no interaction timeout");
      turnOff();
    }
  }
#endif

  // Deep sleep timer: only when OFF and no indicator playing
  static unsigned long offStateStartTime = 0;
  if (currentLampState == OFF && !isPlayingIndicator()) {
    if (offStateStartTime == 0) {
      offStateStartTime = millis();
      DEBUG_PRINTLN("OFF state - deep sleep timer started");
    } else if (millis() - offStateStartTime >= DEEP_SLEEP_TIMEOUT_MS) {
      enterDeepSleep();
    }
  } else {
    offStateStartTime = 0;
  }

  delay(1);  // Minimal delay for smooth transitions
}

void enterDeepSleep() {
  DEBUG_PRINTLN("Entering deep sleep...");
  DEBUG_PRINTLN("Touch button to wake");

  // Ensure LEDs are completely off
  ledcWrite(0, 0);  // WHITE_LED_CHANNEL
  ledcWrite(1, 0);  // WARM_LED_CHANNEL

  // CRITICAL: Set GPIO pins to OUTPUT LOW and hold during deep sleep
  // Prevents GPIO leakage from partially turning on MOSFETs
  pinMode(WHITE_LED_PIN, OUTPUT);
  digitalWrite(WHITE_LED_PIN, LOW);
  gpio_hold_en((gpio_num_t)WHITE_LED_PIN);

  pinMode(WARM_LED_PIN, OUTPUT);
  digitalWrite(WARM_LED_PIN, LOW);
  gpio_hold_en((gpio_num_t)WARM_LED_PIN);

  // Configure wake on GPIO3 (TOUCH_PIN) going HIGH
  esp_deep_sleep_enable_gpio_wakeup(1ULL << TOUCH_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);

  delay(100);  // Allow serial to flush
  esp_deep_sleep_start();
}
