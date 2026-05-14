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
