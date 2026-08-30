#include <Arduino.h>
#include <Wire.h>
#include "accel_input.h"
#include "config.h"

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(LIS3DH_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// Returns 0 rather than Wire.read()'s -1 when the transaction fails (bus glitch, the
// I2C_TIMEOUT_MS bound expiring, the part not answering). That default matters: -1 truncates
// to 0xFF, which has the Sclick bit set — so a wedged bus would have looked like an unbroken
// stream of taps to updateAccelInput() instead of silence. Failing to "no event" degrades to
// "the accelerometer stops responding", which is inert, rather than to phantom gestures.
static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(LIS3DH_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)LIS3DH_I2C_ADDR, (uint8_t)1) != 1) {
        DEBUG_PRINTLN("ACCEL: I2C read failed");
        return 0;
    }
    return (uint8_t)Wire.read();
}

// Both scale factors are fixed by the ±2g full-scale range (CTRL_REG4) and 100 Hz ODR
// (CTRL_REG1) accelInit() always sets — they only hold at those settings.
static uint8_t mgToThs(uint16_t mg) { return (uint8_t)(mg / 16); }        // 16 mg per LSB
static uint8_t msToDuration(uint16_t ms) { return (uint8_t)(ms / 10); }   // 10 ms per LSB @ 100 Hz

// Debug-only: the DEBUG_PRINT macros compile to nothing in a release build, but the readReg()
// calls would not — 14 I2C round trips every boot whose results are then discarded, and up to
// 14 x I2C_TIMEOUT_MS of boot stall if the bus is wedged. Guard the body, not just the prints.
void accelDumpConfig() {
#if DEBUG
    struct { uint8_t reg; const char* name; } regs[] = {
        { 0x0F, "WHO_AM_I  " },  // Should be 0x33
        { 0x20, "CTRL_REG1 " },  // ODR + axes
        { 0x21, "CTRL_REG2 " },  // HPF
        { 0x22, "CTRL_REG3 " },  // INT1 routing (0x80 = I1_CLICK awake, 0x40 = I1_IA1 asleep)
        { 0x23, "CTRL_REG4 " },  // FS
        { 0x24, "CTRL_REG5 " },  // Latch (should have 0x08 = LIR_INT1)
        { 0x30, "INT1_CFG  " },  // Motion-wake axis/event selection
        { 0x32, "INT1_THS  " },  // Motion-wake threshold
        { 0x33, "INT1_DUR  " },  // Motion-wake minimum duration
        { 0x38, "CLICK_CFG " },  // Axes enabled for tap
        { 0x3A, "CLICK_THS " },  // Threshold
        { 0x3B, "TIME_LIMIT" },
        { 0x3C, "TIME_LAT  " },
        { 0x3D, "TIME_WIN  " },
    };
    DEBUG_PRINTLN("ACCEL: --- config dump ---");
    for (auto& r : regs) {
        uint8_t val = readReg(r.reg);
        DEBUG_PRINT("  ");
        DEBUG_PRINT(r.name);
        DEBUG_PRINT(" [0x");
        DEBUG_PRINT2(r.reg, HEX);
        DEBUG_PRINT("] = 0x");
        DEBUG_PRINTLN2(val, HEX);
    }
    DEBUG_PRINTLN("ACCEL: -----------------");
#endif
}

void accelInit() {
    // Own the INT1 line explicitly. updateAccelInput() polls it with digitalRead() every
    // loop() iteration and nothing else configures it — relying on the pin happening to come
    // out of reset (or out of esp_deep_sleep_enable_gpio_wakeup()) as a plain input leaves
    // the gesture engine's only input source unowned.
    pinMode(LIS3DH_INT_PIN, INPUT);

    writeReg(0x20, LIS3DH_CTRL_REG1);  // ODR, low-power mode, axes
    writeReg(0x21, 0x05);               // High-pass filter for click (bit2) and for the
                                         // motion-wake generator (HPIS1, bit0) — without
                                         // HPIS1 the motion generator would compare against
                                         // raw (gravity-included) acceleration, so it'd sit
                                         // permanently above any reasonable threshold on
                                         // whichever axis is vertical.
    writeReg(0x22, 0x80);               // Route click interrupt to INT1 (I1_CLICK) — the
                                         // awake default; enterDeepSleep() switches this to
                                         // I1_IA1 (motion) right before sleeping.
    writeReg(0x23, 0x00);               // FS=±2g, no high-resolution mode
    writeReg(0x24, 0x08);               // Latch INT1 until CLICK_SRC read (LIR_INT1)
    writeReg(0x25, 0x00);               // CTRL_REG6: INT2 unused
    writeReg(0x38, LIS3DH_CLICK_CFG);  // Single + double tap on configured axes
    writeReg(0x3A, LIS3DH_CLICK_THS);  // Tap amplitude threshold
    writeReg(0x3B, LIS3DH_TIME_LIMIT);
    writeReg(0x3C, LIS3DH_TIME_LATENCY);
    writeReg(0x3D, LIS3DH_TIME_WINDOW);
    DEBUG_PRINTLN("ACCEL: config applied (accelInit)");
}

uint8_t accelReadClickSrc() {
    return readReg(0x39);  // Reading CLICK_SRC clears the latched click interrupt
}

uint8_t accelReadInt1Src() {
    return readReg(0x31);  // Reading INT1_SRC clears the latched motion-wake interrupt
}

void accelConfigureWakeMotion(uint16_t ths_mg, uint16_t duration_ms) {
    uint8_t ths = mgToThs(ths_mg);
    uint8_t duration = msToDuration(duration_ms);
    writeReg(0x32, ths);       // INT1_THS
    writeReg(0x33, duration);  // INT1_DURATION
    writeReg(0x30, 0x2A);      // INT1_CFG: OR of X/Y/Z high-threshold events (any axis) — this
                                // arms the AOI generator immediately, and it keeps latching
                                // INT1_SRC in the background for as long as it stays armed, even
                                // while CTRL_REG3 has it unrouted from the pin (i.e. all through
                                // the awake session, since accelInit() never touches INT1_CFG).
                                // Read INT1_SRC now to clear that accumulated latch — otherwise
                                // the very next line, which routes IA1 onto the physical pin,
                                // would surface a stale motion event from earlier in the awake
                                // session and wake the device again immediately.
    accelReadInt1Src();
    writeReg(0x22, 0x40);      // CTRL_REG3: route the motion interrupt (I1_IA1) to INT1,
                                // replacing the click routing accelInit() set at boot
    DEBUG_PRINT("ACCEL: wake-motion config applied (");
    DEBUG_PRINT(ths_mg);
    DEBUG_PRINT("mg, ");
    DEBUG_PRINT(duration_ms);
    DEBUG_PRINTLN("ms)");
}
