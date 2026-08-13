#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
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
static void debugClickSrc(uint8_t src) {
  DEBUG_PRINT("ACCEL: CLICK_SRC=0x");
  DEBUG_PRINT2(src, HEX);
  DEBUG_PRINT(" [");
  if (!src) { DEBUG_PRINTLN("empty]"); return; }
  if (src & 0x40) DEBUG_PRINT("IA ");
  if (src & 0x20) DEBUG_PRINT("Dclick ");
  if (src & 0x10) DEBUG_PRINT("Sclick ");
  if (src & 0x04) DEBUG_PRINT("Z");
  if (src & 0x02) DEBUG_PRINT("Y");
  if (src & 0x01) DEBUG_PRINT("X");
  if (src & 0x07) DEBUG_PRINT(src & 0x08 ? "- " : "+ ");
  DEBUG_PRINTLN("]");
}

// Non-blocking state machine. The accelerometer is now only an auxiliary trigger for the
// mode-swap gesture (on/off/brightness/battery indicator all live on the physical button),
// so there's no single-vs-double-tap discrimination to do any more: any detected tap swaps
// the mode. A cooldown after dispatch suppresses ring-down re-triggers from the same tap.
static void updateAccelInput() {
  enum AccelState { IDLE, COOLDOWN };
  static AccelState state = IDLE;
  static unsigned long stateStart = 0;

  if (state == IDLE) {
    if (digitalRead(LIS3DH_INT_PIN) == HIGH) {
      uint8_t src = accelReadClickSrc();
      DEBUG_PRINT("ACCEL: tap detected: ");
      debugClickSrc(src);
      if ((src >> 4) & 0x01) {  // Sclick
        DEBUG_PRINTLN("ACCEL: tap → swap mode");
        handleDoubleTap();
      }
      stateStart = millis();
      state = COOLDOWN;
    }
  } else {
    if (millis() - stateStart >= LIS3DH_COOLDOWN_MS) {
      state = IDLE;
    }
  }
}
#endif

// Task watchdog: reboots the device if loop() ever fails to check in for
// WATCHDOG_TIMEOUT_MS (e.g. an I2C bus lockup on the accelerometer link, or any
// future bug that blocks the loop). The API shape changed between ESP-IDF 4 and 5
// (Arduino core versions), so branch on the IDF major version to support both.
static void initWatchdog() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WATCHDOG_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_err_t err = esp_task_wdt_init(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE) {
    // Framework already initialized the TWDT with its own defaults — apply ours instead.
    esp_task_wdt_reconfigure(&wdtConfig);
  }
#else
  esp_task_wdt_init(WATCHDOG_TIMEOUT_MS / 1000, true);
#endif

  esp_err_t addErr = esp_task_wdt_add(NULL);  // Subscribe the loop task (current task)
  if (addErr != ESP_OK && addErr != ESP_ERR_INVALID_STATE) {
    DEBUG_PRINTLN("WARNING: failed to subscribe loop task to watchdog");
  }
  DEBUG_PRINTLN("Watchdog armed");
}

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
  initWatchdog();

#ifdef USE_ACCEL_INPUT
  Wire.begin(LIS3DH_SDA_PIN, LIS3DH_SCL_PIN);
  Wire.setTimeOut(I2C_TIMEOUT_MS);  // Bound I2C transactions so a bus glitch can't block loop()
  accelInit();
  accelDumpConfig();
  // Flush any latched INT1 left over from before a reset/reflash so the first loop()
  // iteration doesn't immediately read it as a fresh tap.
  accelReadClickSrc();
#endif

  // Register callbacks — the physical button always drives the full gesture set.
  setSingleTapCallback(handleSingleTap);
  setDoubleTapCallback(handleDoubleTap);
  setTripleTapCallback(handleTripleTap);
  setLongPressStartCallback(handleLongPressStart);
  setLongPressHoldCallback(handleLongPressHold);
  setLongPressEndCallback(handleLongPressEnd);

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    DEBUG_PRINTLN("Woke from deep sleep!");
    lastInteractionTime = millis();

    // Disable GPIO hold to allow PWM control again
    gpio_hold_dis((gpio_num_t)WHITE_LED_PIN);
    gpio_hold_dis((gpio_num_t)WARM_LED_PIN);

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
  DEBUG_PRINTLN("Button single tap: toggle ON/OFF");
  DEBUG_PRINTLN("Button double tap (or accel tap): swap WARM/COOL");
  DEBUG_PRINTLN("Button long press: adjust brightness");
  DEBUG_PRINTLN("Button triple tap: battery indicator");
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
  updateButton();
#ifdef USE_ACCEL_INPUT
  updateAccelInput();
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

  // Deep sleep timer: only when OFF and no indicator playing.
  // Uses an explicit "running" flag rather than treating offStateStartTime == 0 as
  // "not started" — millis() legitimately returns 0 briefly after boot, which would
  // otherwise make the timer think it needs to (re-)start forever at that instant.
  static unsigned long offStateStartTime = 0;
  static bool offTimerRunning = false;
  if (currentLampState == OFF && !isPlayingIndicator()) {
    if (!offTimerRunning) {
      offStateStartTime = millis();
      offTimerRunning = true;
      DEBUG_PRINTLN("OFF state - deep sleep timer started");
    } else if (millis() - offStateStartTime >= DEEP_SLEEP_TIMEOUT_MS) {
      enterDeepSleep();
    }
  } else {
    offTimerRunning = false;
  }

  // Pet the watchdog — if loop() ever fails to reach here within WATCHDOG_TIMEOUT_MS
  // (e.g. an I2C hang), the device reboots instead of staying frozen.
  esp_task_wdt_reset();

  delay(1);  // Minimal delay for smooth transitions
}

void enterDeepSleep() {
  DEBUG_PRINTLN("Entering deep sleep...");
  DEBUG_PRINTLN("Press button to wake");

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

  // Configure wake on BUTTON_PIN going HIGH (accelerometer is not a wake source)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);

  delay(100);  // Allow serial to flush
  esp_deep_sleep_start();
}
