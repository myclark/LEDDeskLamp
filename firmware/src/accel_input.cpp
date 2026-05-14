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

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(LIS3DH_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)LIS3DH_I2C_ADDR, (uint8_t)1);
    return Wire.read();
}

void accelDumpConfig() {
    struct { uint8_t reg; const char* name; } regs[] = {
        { 0x0F, "WHO_AM_I  " },  // Should be 0x33
        { 0x20, "CTRL_REG1 " },  // ODR + axes
        { 0x21, "CTRL_REG2 " },  // HPF
        { 0x22, "CTRL_REG3 " },  // INT1 routing (should have 0x80 = I1_CLICK)
        { 0x23, "CTRL_REG4 " },  // FS
        { 0x24, "CTRL_REG5 " },  // Latch (should have 0x08 = LIR_INT1)
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
}

void accelInit() {
    writeReg(0x20, LIS3DH_CTRL_REG1);  // ODR, low-power mode, axes
    writeReg(0x21, 0x04);               // High-pass filter enabled for click only
    writeReg(0x22, 0x80);               // Route click interrupt to INT1 (I1_CLICK)
    writeReg(0x23, 0x00);               // FS=±2g, no high-resolution mode
    writeReg(0x24, 0x08);               // Latch INT1 until CLICK_SRC read (LIR_INT1)
    writeReg(0x25, 0x00);               // CTRL_REG6: INT2 unused
    writeReg(0x38, LIS3DH_CLICK_CFG);  // Single + double tap on configured axes
    writeReg(0x3A, LIS3DH_CLICK_THS);  // Tap amplitude threshold
    writeReg(0x3B, LIS3DH_TIME_LIMIT);
    writeReg(0x3C, LIS3DH_TIME_LATENCY);
    writeReg(0x3D, LIS3DH_TIME_WINDOW);
}

uint8_t accelReadClickSrc() {
    return readReg(0x39);  // Reading CLICK_SRC clears the latched INT1
}
