#pragma once
#include <stdint.h>

void accelInit();            // I2C + LIS3DH register initialisation
void accelDumpConfig();      // Read back and print all key config registers
uint8_t accelReadClickSrc(); // Read CLICK_SRC (0x39), clearing the latched interrupt

// Reprograms CLICK_THS (0x3A) alone, leaving every other register untouched. Used to swap
// between the normal (awake) tap threshold and a more sensitive one right before deep sleep,
// so a gentle jostle reliably wakes the device without permanently loosening the threshold
// used for the double/triple-tap mode-swap gesture — accelInit() puts CLICK_THS back to
// LIS3DH_CLICK_THS on the next boot, before any gesture detection runs.
void accelSetClickThreshold(uint8_t ths);
