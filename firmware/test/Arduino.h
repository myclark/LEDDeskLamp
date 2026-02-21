// Mock Arduino.h for native testing ONLY
// This file is in test/ folder - ONLY active when NATIVE is defined
#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#ifdef NATIVE
// Native platform - provide mocks
#include <stdint.h>

typedef uint8_t byte;
#define ADC_11db 0
#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1

class SerialClass {
public:
  template <typename T> void print(T value) {}
  template <typename T> void println(T value) {}
  void println() {}
};

extern SerialClass Serial;

unsigned long millis();
int analogRead(uint8_t pin);
int digitalRead(uint8_t pin);
void delay(unsigned long ms);
void pinMode(uint8_t pin, uint8_t mode);
void analogReadResolution(uint8_t bits);
void analogSetAttenuation(uint8_t attenuation);

#else
// ESP32 platform - use real Arduino.h from framework
#include_next <Arduino.h>
#endif  // NATIVE

#endif  // ARDUINO_MOCK_H
