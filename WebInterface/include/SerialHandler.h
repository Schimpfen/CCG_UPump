// SerialHandler.h

#pragma once
#include <Arduino.h>

// Initialize the Serial CLI and announce the current I2C settings.
void SerialHandler_begin(int sda_pin, int scl_pin, uint32_t i2c_hz);

// Poll the serial port and handle commands (call from loop()).
void SerialHandler_poll();
