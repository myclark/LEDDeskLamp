# LIS3DH Tap Input — Integration Notes

Replaces the TTP223 capacitive touch module. The LIS3DH (SparkFun SEN-13963) detects
physical taps on the lamp body via its hardware click-detection engine, with a single
interrupt line waking the ESP32-C3 from deep sleep.

---

## Wiring

| LIS3DH Pin | ESP32-C3 SuperMini | Notes |
|---|---|---|
| VCC | 3.3V | 3.3V device — do not connect to 5V |
| GND | GND | |
| SDA | GPIO8 | I2C data (default ESP32-C3 I2C SDA) |
| SCL | GPIO9 | I2C clock (default ESP32-C3 I2C SCL) |
| I1 | GPIO3 | Interrupt + deep sleep wakeup (replaces TTP223 OUT) |
| I2 | — | Not used |
| !CS | — | Leave unconnected (I2C mode) |
| SDO | — | Leave unconnected |
| A1/A2/A3 | — | Leave unconnected |

The board has I2C pull-ups fitted by default (jumper closed). The interrupt line reuses
the same GPIO3 that was previously connected to the TTP223 OUT pin.

**I2C address:** `0x19` by default. Bridge the bottom address jumper to use `0x18`.

---

## config.h Additions

```cpp
// ── LIS3DH ────────────────────────────────────────────────────────────────────

// I2C & interrupt pins
#define LIS3DH_SDA_PIN       8       // I2C SDA
#define LIS3DH_SCL_PIN       9       // I2C SCL
#define LIS3DH_INT_PIN       3       // INT1 → deep sleep ext0 wakeup (replaces TOUCH_PIN)
#define LIS3DH_I2C_ADDR      0x19    // Default; 0x18 if address jumper bridged

// Axis enable (CLICK_CFG register 0x38)
// Enable the axes that will respond to taps.
// Bits: [-, -, ZD, ZS, YD, YS, XD, XS]
//   ZD/YD/XD = double-tap enable per axis
//   ZS/YS/XS = single-tap enable per axis
// Z-axis up (lamp tapped from the top):   0x30  ← default
// All axes (useful for debugging):        0x3F
// X+Y only (accelerometer mounted flat):  0x0F
#define LIS3DH_CLICK_CFG     0x30

// Tap amplitude threshold (CLICK_THS register 0x3A)
// 1 LSB = FS/128 = ~16 mg at FS=±2g (CTRL_REG4 default)
// Range: 0x00–0x7F.  Start at 0x20 (~512 mg) and tune down if taps are missed.
// Lower value = more sensitive. Too low = false triggers from vibration.
#define LIS3DH_CLICK_THS     0x20

// Output data rate (CTRL_REG1 0x20)
// Sets time resolution of all timing registers (1 LSB = 1/ODR).
// 0x57 = ODR 100 Hz, low-power mode, X+Y+Z enabled (~6 µA)
// 0x5F = ODR 100 Hz, low-power mode, all axes  (same power, use if CLICK_CFG = 0x3F)
// Change ODR only if timing values below need finer resolution (e.g. 0x67 for 200 Hz).
#define LIS3DH_CTRL_REG1     0x57

// Tap timing — all values in ODR ticks (1 tick = 1/ODR = 10 ms at 100 Hz)
//
// TIME_LIMIT (0x3B): maximum duration of a single tap impulse.
// The acceleration must exceed the threshold and return below it within this window.
// Too short → sharp taps rejected.  Too long → slow presses misread as taps.
// 0x08 = 80 ms at 100 Hz  ← start here
#define LIS3DH_TIME_LIMIT    0x08

// TIME_LATENCY (0x3C): dead time after the first tap during which a second tap
// is ignored.  Prevents the same physical impulse ringing into a false double-tap.
// 0x10 = 160 ms at 100 Hz  ← start here
#define LIS3DH_TIME_LATENCY  0x10

// TIME_WINDOW (0x3D): window after TIME_LATENCY in which the second tap must arrive
// for a double-tap to be recognised.
// Total double-tap acceptance window = TIME_LATENCY + TIME_WINDOW.
// 0x18 = 240 ms at 100 Hz → total window = 160 + 240 = 400 ms  ← start here
#define LIS3DH_TIME_WINDOW   0x18
```

The existing `TOUCH_PIN` define is superseded by `LIS3DH_INT_PIN`. The other pin
defines (`WHITE_LED_PIN`, `WARM_LED_PIN`, `BATTERY_PIN`) are unchanged.

---

## Gesture Behaviour

### While OFF (deep sleep)

Any tap — single or double — simply wakes the lamp and turns it on. No gesture
discrimination is performed on wakeup. The act of tapping is the intent; which tap
it was is irrelevant.

Implementation: on wakeup from `LIS3DH_INT_PIN` (ext0), read and discard `CLICK_SRC`
to clear the latched interrupt, then proceed straight to the ON state.

### While ON (awake)

| Gesture | Action |
|---|---|
| Single tap | Toggle off |
| Double tap | Switch mode (warm ↔ cool) |

Because the hardware fires a single-tap interrupt immediately and a double-tap interrupt
only after the full `TIME_LATENCY + TIME_WINDOW` period, the firmware must wait before
acting. After `INT1` fires while the lamp is on:

1. Wait `TIME_LATENCY + TIME_WINDOW` (≈ 400 ms with default values)
2. Read `CLICK_SRC` (register `0x39`)
3. If `Dtap` bit (bit 5) is set → switch mode
4. Otherwise (`Stap` bit 4 only) → toggle off

This introduces a ~400 ms latency on deliberate taps while the lamp is on, which is
acceptable for a lamp. Reduce `TIME_WINDOW` to shorten it if needed.

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

In power-down mode (ODR bits = `0000` in CTRL_REG1) the LIS3DH draws ~0.4 µA —
lower than the TTP223's ~1.5 µA standby. However, the click detection engine is
inactive in power-down mode; the lamp relies on deep sleep wakeup via the latched
`INT1` from the last tap, so the chip should remain in its configured low-power ODR
mode rather than powered-down while the system is asleep.

At 100 Hz low-power mode, current draw is ~6 µA, replacing the TTP223's ~1.5 µA.
The net change to the deep sleep budget is approximately **+4.5 µA**, bringing the
estimated total from ~22.5 µA to ~27 µA — still comfortably negligible.

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
