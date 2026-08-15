#pragma once
#include <stdint.h>

void accelInit();            // I2C + LIS3DH register initialisation
void accelDumpConfig();      // Read back and print all key config registers
uint8_t accelReadClickSrc(); // Read CLICK_SRC (0x39), clearing the latched click interrupt
uint8_t accelReadInt1Src();  // Read INT1_SRC (0x31), clearing the latched motion-wake interrupt

// Reprograms INT1 as a general motion-threshold ("wiggle") wake source, routing the LIS3DH's
// AOI/IA1 interrupt generator onto INT1 in place of the tap/click detector used while awake —
// so ANY axis exceeding ths_mg for at least duration_ms wakes the device, not just a
// tap-shaped impulse. Values are given directly in mg/ms; converted internally to raw
// register units (see mgToThs()/msToDuration() in accel_input.cpp). Used only right before
// deep sleep — accelInit() puts INT1 routing back to the tap/click detector on the next boot,
// before any gesture detection runs, so this never leaks into awake gesture sensitivity.
void accelConfigureWakeMotion(uint16_t ths_mg, uint16_t duration_ms);
