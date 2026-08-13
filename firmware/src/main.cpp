#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "config.h"
#include "led_control.h"
#include "touch_input.h"
#include "battery_monitor.h"
#ifdef USE_POT_INPUT
#include "pot_input.h"
#endif
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

// Tracks the recurring low-battery reminder (see loop()): when the indicator was last
// shown, and which state it was last shown for, so the periodic reminder knows both when
// to repeat and when to fire immediately because the battery just got worse.
static unsigned long lastBatteryIndicatorTime = 0;
static BatteryState lastAnnouncedBatteryState = BATTERY_NORMAL;

// Every trigger site (turn-on, wake, on-demand triple tap, and the periodic reminder in
// loop()) goes through this so they all share one "when did we last show it" clock —
// otherwise the periodic reminder would have no way to know a turn-on/wake/manual check
// already covered the user just now.
static void showBatteryIndicator(BatteryState state) {
  playBatteryIndicator(state);
  lastBatteryIndicatorTime = millis();
  lastAnnouncedBatteryState = state;
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
      showBatteryIndicator(batteryState);
    }
  }
}

// Callback: double tap (button) or accelerometer tap — swap warm/cool (ignored when OFF)
void handleDoubleTap() {
  DEBUG_PRINTLN(">>> DOUBLE TAP / ACCEL TAP: swap mode");
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  savedMode = (savedMode == MODE_WARM) ? MODE_COOL : MODE_WARM;
#ifdef USE_POT_INPUT
  // Brightness isn't stored per mode in pot mode — it's always just wherever the pot
  // currently points, so swap to that rather than a remembered value.
  uint8_t target = getPotBrightnessTarget();
  swapMode(savedMode, target);
  setBrightnessTarget(target);
#else
  swapMode(savedMode, *getModeBrightness(savedMode));
#endif
}

// Callback: triple tap — show battery level indicator (ignored when OFF)
void handleTripleTap() {
  DEBUG_PRINTLN(">>> TRIPLE TAP");
  lastInteractionTime = millis();
  if (currentLampState != ON) return;

  readBatteryVoltage();
  setTouchBlocked(true);
  showBatteryIndicator(getBatteryState());
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

#ifdef USE_POT_INPUT
// Drives on/off and brightness from the potentiometer every loop() iteration. Unlike the
// button, the pot has no discrete "tap" — it's a continuously polled position, so this
// isn't a gesture callback, it's compared each tick against the lamp's current state to
// detect ON/OFF edges.
static void updatePotControl() {
  updatePotInput();
  bool wantsOn = isPotRequestingOn();
  uint8_t target = getPotBrightnessTarget();
  static uint8_t lastInteractionTarget = 0;

  if (wantsOn && currentLampState != ON) {
    DEBUG_PRINTLN(">>> POT: requesting ON");
    lastInteractionTime = millis();
    lastInteractionTarget = target;

    readBatteryVoltage();
    BatteryState batteryState = getBatteryState();
    if (batteryState == BATTERY_CUTOFF) {
      DEBUG_PRINTLN("Battery CUTOFF - refusing to turn on, entering deep sleep");
      enterDeepSleep();
    }

    turnOn(savedMode, target);
    setBrightnessTarget(target);

    if (batteryState == BATTERY_LOW || batteryState == BATTERY_CRITICAL) {
      showBatteryIndicator(batteryState);
    }
  } else if (!wantsOn && currentLampState == ON) {
    DEBUG_PRINTLN(">>> POT: requesting OFF");
    lastInteractionTime = millis();
    turnOff();
  } else if (wantsOn && currentLampState == ON) {
    setBrightnessTarget(target);
    // Only count real movement as interaction — filters ADC jitter that would
    // otherwise reset the auto-off timer forever.
    if (abs((int)target - (int)lastInteractionTarget) > POT_MOVEMENT_DEADBAND) {
      lastInteractionTime = millis();
      lastInteractionTarget = target;
    }
  }
}
#endif

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

// Non-blocking state machine. Mode swap requires a double- or triple-tap — a single tap is
// deliberately ignored, since the lamp body gets bumped constantly during ordinary use (in
// pot mode especially: turning the knob shakes the enclosure the accelerometer is mounted
// to). Counting to at least 2 before dispatching turns those incidental knocks into no-ops
// while still recognizing an intentional multi-tap gesture.
//
//   IDLE/WAITING  → INT1 fires (Sclick) → tapCount++, RING_SUPPRESS (absorb this tap's ringing)
//   RING_SUPPRESS → wait LIS3DH_RING_SUPPRESS_MS → WAITING (watch for the next tap)
//   WAITING       → another tap arrives → back to RING_SUPPRESS; or
//                   LIS3DH_GESTURE_WINDOW_MS passes with no new tap → window closes:
//                     tapCount >= 2 → swap mode; tapCount == 1 → discarded as incidental
static void updateAccelInput() {
  enum AccelState { IDLE, RING_SUPPRESS, WAITING };
  static AccelState state = IDLE;
  static uint8_t tapCount = 0;
  static unsigned long lastTapTime = 0;
  static unsigned long suppressStart = 0;

  if (state == RING_SUPPRESS) {
    if (millis() - suppressStart >= LIS3DH_RING_SUPPRESS_MS) {
      state = WAITING;
    }
    return;  // Ignore INT1 entirely while suppressing this tap's ring-down
  }

  if (state == WAITING && millis() - lastTapTime >= LIS3DH_GESTURE_WINDOW_MS) {
    if (tapCount >= 2) {
      DEBUG_PRINTLN("ACCEL: multi-tap → swap mode");
      handleDoubleTap();
    } else {
      DEBUG_PRINTLN("ACCEL: single tap ignored (need a double/triple tap to swap mode)");
    }
    tapCount = 0;
    state = IDLE;
  }

  if (digitalRead(LIS3DH_INT_PIN) == HIGH) {
    uint8_t src = accelReadClickSrc();
    DEBUG_PRINT("ACCEL: tap detected: ");
    debugClickSrc(src);
    if ((src >> 4) & 0x01) {  // Sclick
      tapCount++;
      lastTapTime = millis();
      suppressStart = millis();
      state = RING_SUPPRESS;
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

#ifdef USE_POT_INPUT
  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    // Release the hold before initPotInput() reconfigures this pin — a held pin won't
    // respond to pinMode()/digitalWrite() until the hold is explicitly disabled.
    gpio_hold_dis((gpio_num_t)POT_POWER_PIN);
  }
#endif

  // Initialize modules
#ifdef USE_POT_INPUT
  initPotInput();
  // Let the RC front end settle before trusting any reading — covers both this wake path
  // and a fresh power-on, since initPotInput() (and thus potPowerOn()) always runs here.
  delay(POT_SETTLE_MS);
#else
  initTouch();
#endif
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

#ifndef USE_POT_INPUT
  // Register callbacks — the physical button drives the full gesture set.
  setSingleTapCallback(handleSingleTap);
  setDoubleTapCallback(handleDoubleTap);
  setTripleTapCallback(handleTripleTap);
  setLongPressStartCallback(handleLongPressStart);
  setLongPressHoldCallback(handleLongPressHold);
  setLongPressEndCallback(handleLongPressEnd);
#endif

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
    DEBUG_PRINTLN("Woke from deep sleep!");

    // Disable GPIO hold to allow PWM control again
    gpio_hold_dis((gpio_num_t)WHITE_LED_PIN);
    gpio_hold_dis((gpio_num_t)WARM_LED_PIN);

#ifdef USE_POT_INPUT
    // The accelerometer tap that woke us doesn't necessarily mean "turn on" — it might
    // just be the bump of a hand reaching for the dial. Clear its latch, then take a
    // fresh pot reading and let that decide: if the pot itself is still at OFF, stay OFF
    // and let the deep-sleep timer below put the device straight back to sleep.
    accelReadClickSrc();
    updatePotInput();
    if (isPotRequestingOn()) {
      lastInteractionTime = millis();
      uint8_t target = getPotBrightnessTarget();
      turnOn(savedMode, target);
      setBrightnessTarget(target);

      readBatteryVoltage();
      BatteryState batteryState = getBatteryState();
      if (batteryState == BATTERY_LOW || batteryState == BATTERY_CRITICAL) {
        showBatteryIndicator(batteryState);
      }
    } else {
      DEBUG_PRINTLN("Woke but pot is still at OFF — going back to sleep shortly");
    }
#else
    lastInteractionTime = millis();
    // Restore saved mode and brightness
    turnOn(savedMode, *getModeBrightness(savedMode));

    // Auto battery indicator when LOW or CRITICAL on wake
    readBatteryVoltage();
    BatteryState batteryState = getBatteryState();
    if (batteryState == BATTERY_LOW || batteryState == BATTERY_CRITICAL) {
      setTouchBlocked(true);
      showBatteryIndicator(batteryState);
    }
#endif
  } else {
    DEBUG_PRINTLN("Power-on or reset (staying in OFF)");
    DEBUG_PRINT("Saved mode: ");
    DEBUG_PRINTLN(savedMode == MODE_WARM ? "WARM" : "COOL");
#ifndef USE_POT_INPUT
    DEBUG_PRINT("Warm brightness: ");
    DEBUG_PRINT(warmBrightness);
    DEBUG_PRINT(", Cool brightness: ");
    DEBUG_PRINTLN(coolBrightness);
#endif
  }

  DEBUG_PRINTLN("Lamp ready");
#ifdef USE_POT_INPUT
  DEBUG_PRINTLN("Pot: turn to set brightness, fully counter-clockwise = OFF");
  DEBUG_PRINTLN("Accel tap: swap WARM/COOL");
#else
  DEBUG_PRINTLN("Button single tap: toggle ON/OFF");
  DEBUG_PRINTLN("Button double tap (or accel tap): swap WARM/COOL");
  DEBUG_PRINTLN("Button long press: adjust brightness");
  DEBUG_PRINTLN("Button triple tap: battery indicator");
#endif
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
#ifdef USE_POT_INPUT
  updatePotControl();
#else
  updateButton();
#endif
#ifdef USE_ACCEL_INPUT
  updateAccelInput();
#endif
  updateModeTransition();
#ifdef USE_POT_INPUT
  updateBrightnessSlew();
#endif
  updateBatteryMonitor();
  updateBatteryIndicator();

  // Unblock touch when indicator animation finishes
  static bool indicatorWasPlaying = false;
  bool indicatorNowPlaying = isPlayingIndicator();
  if (indicatorWasPlaying && !indicatorNowPlaying) {
    setTouchBlocked(false);
  }
  indicatorWasPlaying = indicatorNowPlaying;

  // Recurring low-battery reminder while ON: fires immediately the moment the battery
  // worsens into LOW/CRITICAL (whether that's a fresh drain while already ON, or a
  // degrade from LOW to CRITICAL), then repeats periodically for as long as it stays
  // that way. Turn-on/wake/on-demand triggers elsewhere already cover "just started using
  // it with a bad battery" — this covers "still using it, and it's gotten worse or it's
  // been a while since the last reminder."
  if (currentLampState == ON && !isPlayingIndicator()) {
    BatteryState bs = getBatteryState();
    if (bs == BATTERY_LOW || bs == BATTERY_CRITICAL) {
      unsigned long repeatInterval = (bs == BATTERY_CRITICAL)
        ? BATTERY_INDICATOR_REPEAT_CRITICAL_MS : BATTERY_INDICATOR_REPEAT_LOW_MS;
      bool worsenedSinceLastShown = (bs != lastAnnouncedBatteryState);
      if (worsenedSinceLastShown || millis() - lastBatteryIndicatorTime >= repeatInterval) {
        DEBUG_PRINTLN(">>> Recurring low-battery reminder");
#ifndef USE_POT_INPUT
        setTouchBlocked(true);
#endif
        showBatteryIndicator(bs);
      }
    } else {
      // Battery recovered — clear the "worsened" memory so a future dip announces
      // immediately again instead of waiting out a stale repeat interval.
      lastAnnouncedBatteryState = bs;
    }
  }

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
#ifdef USE_POT_INPUT
  DEBUG_PRINTLN("Tap/bump the lamp to wake");
#else
  DEBUG_PRINTLN("Press button to wake");
#endif

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

#ifdef USE_POT_INPUT
  // Cut power to the pot's divider and latch it LOW through sleep — see the
  // POT_POWER_PIN comment in config.h for why this needs no external switch transistor.
  potPowerOff();
  gpio_hold_en((gpio_num_t)POT_POWER_PIN);

  // No physical button in this configuration — the accelerometer tap is the sole wake
  // source (config.h enforces USE_ACCEL_INPUT whenever USE_POT_INPUT is defined).
  esp_deep_sleep_enable_gpio_wakeup(1ULL << LIS3DH_INT_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
  // Configure wake on BUTTON_PIN going HIGH (accelerometer is not a wake source)
  esp_deep_sleep_enable_gpio_wakeup(1ULL << BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif

  delay(100);  // Allow serial to flush
  esp_deep_sleep_start();
}
