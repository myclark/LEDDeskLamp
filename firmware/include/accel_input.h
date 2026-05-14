#pragma once
#include <stdint.h>

void accelInit();           // I2C + LIS3DH register initialisation
uint8_t accelReadClickSrc(); // Read CLICK_SRC (0x39), clearing the latched interrupt
