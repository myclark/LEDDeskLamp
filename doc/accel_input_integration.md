# LIS3DH Tap Input — Integration Notes

Auxiliary input alongside the physical on/off button (see `doc/firmware_architecture.md`).
The LIS3DH (SparkFun SEN-13963) detects physical taps on the lamp body via its hardware
click-detection engine and is used **only** to trigger the mode-swap gesture (WARM ↔ COOL) —
it is not a wake source and does not control power state.

---

## Wiring

| LIS3DH Pin | ESP32-C3 SuperMini | Notes |
|---|---|---|
| VCC | 3.3V | 3.3V device — do not connect to 5V |
| GND | GND | |
| SDA | GPIO8 | I2C data (default ESP32-C3 I2C SDA) |
| SCL | GPIO9 | I2C clock (default ESP32-C3 I2C SCL) |
| I1 | GPIO4 | Interrupt only — not a deep-sleep wakeup source |
| I2 | — | Not used |
| !CS | — | Leave unconnected (I2C mode) |
| SDO | — | Leave unconnected |
| A1/A2/A3 | — | Leave unconnected |

The board has I2C pull-ups fitted by default (jumper closed). GPIO4 is dedicated to the
accelerometer interrupt now that GPIO3 belongs exclusively to the physical on/off button
(`BUTTON_PIN` — see `doc/firmware_architecture.md`).

**I2C address:** `0x19` by default. Bridge the bottom address jumper to use `0x18`.

**I2C robustness:** `Wire.setTimeOut(I2C_TIMEOUT_MS)` is set in `setup()` so a bus glitch
(e.g. from LED PWM switching noise) fails a transaction fast instead of blocking `loop()`;
a task watchdog (`esp_task_wdt`) is also armed as a backstop that reboots the device if
`loop()` ever stalls regardless of cause. See the Timeout Audit & Watchdog section in
`doc/firmware_architecture.md`.

---

## config.h Values

All LIS3DH register values (`LIS3DH_CLICK_CFG`, `LIS3DH_CLICK_THS`, `LIS3DH_CTRL_REG1`,
timing registers, pins) live in `firmware/include/config.h` — that file is the source of
truth; don't duplicate values here where they can drift out of sync. Current defaults use
single-tap detection on all axes (`LIS3DH_CLICK_CFG = 0x15`) since the firmware no longer
needs hardware or firmware double-tap discrimination — see Gesture Behaviour below.

---

## Gesture Behaviour

The accelerometer plays no role while the lamp is OFF or asleep — it is not a deep-sleep
wakeup source, and its interrupt line (`LIS3DH_INT_PIN`, now GPIO4) is independent of the
button's wake pin (`BUTTON_PIN`, GPIO3).

### While ON (awake)

| Gesture | Action |
|---|---|
| Tap on lamp body | Switch mode (warm ↔ cool) — same action as double-tapping the button |

Any detected tap (`Sclick` bit in `CLICK_SRC`) immediately fires the mode-swap gesture —
there's no single-vs-double discrimination to wait on, so no latency beyond reading the
register. After dispatch, `LIS3DH_COOLDOWN_MS` (300 ms) suppresses re-triggers from the
same tap's ring-down before the state machine re-arms.

On/off, brightness, and the battery indicator are handled entirely by the physical button
(`touch_input.cpp`) and never touch the accelerometer path.

---

## Register Initialisation Sequence

```cpp
// Call once during setup, after Wire.begin(LIS3DH_SDA_PIN, LIS3DH_SCL_PIN)

void lis3dh_init() {
    // ODR, low-power mode, axes
    writeReg(0x20, LIS3DH_CTRL_REG1);

    // High-pass filter enabled for click only — reduces false triggers from
    // slow tilts or vibration.  HPCLICK = bit 2.
    writeReg(0x21, 0x04);

    // Route click interrupt to INT1 pin.  I1_CLICK = bit 7.
    writeReg(0x22, 0x80);

    // FS = ±2g, no high-resolution mode (consistent with low-power ODR setting)
    writeReg(0x23, 0x00);

    // Latch INT1 interrupt — keeps INT1 high after wakeup until CLICK_SRC is read.
    // LIR_INT1 = bit 3.
    writeReg(0x24, 0x08);

    // CTRL_REG6: default — INT2 not used
    writeReg(0x25, 0x00);

    // Enable single + double tap on configured axes
    writeReg(0x38, LIS3DH_CLICK_CFG);

    // Tap threshold
    writeReg(0x3A, LIS3DH_CLICK_THS);

    // Timing
    writeReg(0x3B, LIS3DH_TIME_LIMIT);
    writeReg(0x3C, LIS3DH_TIME_LATENCY);
    writeReg(0x3D, LIS3DH_TIME_WINDOW);
}
```

To read and clear the interrupt:

```cpp
uint8_t src = readReg(0x39);   // CLICK_SRC — reading this clears the latch
bool single_tap  = (src >> 4) & 0x01;  // Stap bit
bool double_tap  = (src >> 5) & 0x01;  // Dtap bit
// bool active   = (src >> 6) & 0x01;  // IA bit — any click event active
```

---

## Power

The accelerometer is not involved in deep sleep wakeup (the button is the sole wake
source), so it could in principle be powered down while the lamp sleeps. It is left in
its configured 100 Hz low-power ODR mode (~6 µA) continuously instead, for simplicity —
the click engine only needs to be live while the lamp is ON to catch a mode-swap tap, but
there's no harm leaving it running through sleep too, and it avoids extra power-sequencing
code. Net addition to the deep sleep budget vs. no accelerometer at all is ~6 µA, see the
Power Management section in `doc/firmware_architecture.md`.

---

## Tuning Notes

All timing and threshold values require empirical adjustment once the sensor is
physically mounted in the lamp body:

- **`LIS3DH_CLICK_THS`** — if taps are frequently missed, lower the value; if the lamp
  triggers from being set down on a surface, raise it.
- **`LIS3DH_TIME_LIMIT`** — if fast sharp taps are rejected, raise it; if slow presses
  falsely trigger taps, lower it.
- **`LIS3DH_TIME_LATENCY`** — raise if the physical impulse of the first tap rings into
  a spurious second detection; lower to allow faster double-taps.
- **`LIS3DH_TIME_WINDOW`** — controls how quickly the user must complete a double-tap.
  400 ms total is a comfortable starting point.
- **`LIS3DH_CLICK_CFG`** — if Z-axis taps are unreliable with the sensor mounted at an
  angle, enable additional axes (`0x3F`) and check the Z/Y/X bits of `CLICK_SRC`
  (bits 2–0) to see which axis is actually firing, then narrow the config accordingly.
