#include "types.h"

#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

// --- System Pins ---
constexpr uint8_t PIN_RESET_SETTINGS = 3;
constexpr uint8_t PIN_BUZZER = 39;
constexpr uint8_t PIN_PWR = 40; // Power control

// --- I2C / Screen Pins ---
constexpr int8_t PIN_SDA = 16; // GPIO8 as I2C SDA
constexpr int8_t PIN_SCL = 17; // GPIO9 as I2C SCL
constexpr int8_t PIN_OLED_RESET = -1; // Reset pin # (-1 if sharing Arduino reset pin)

// --- LED Pins ---
constexpr uint8_t PIN_LED_HEADER_1 = 47; // LED header #1 Data
constexpr uint8_t PIN_LED_HEADER_2 = 48; // LED header #2 Data

constexpr uint8_t PIN_LED_EXT_CTRL_1 = 36; // LED header #1 External Control
constexpr uint8_t PIN_LED_EXT_CTRL_2 = 35; // LED header #2 External Control

// --- Fan Pins ---
extern std::map<int, FanPinPair> PIN_FAN_MAP;

#endif // PINS_H