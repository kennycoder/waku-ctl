#ifndef GLOBALS_H
#define GLOBALS_H

#include <map>
#include <vector>
#include <stdint.h>
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_sntp.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FastLED.h"
#include "ArduinoJson.h"
#include "ESPAsyncWebServer.h"
#include <Adafruit_ADS1X15.h>
#include "USB.h"
#include "USBCDC.h"

#include "types.h"
#include "config_constants.h"
#include "pins.h"

// --- Global Variables ---

// System State
extern unsigned long gHoldButtonCounter;
extern bool b_TempAlarmFiring;
extern bool b_RpmAlarmFiring;
extern bool b_ResetPressed;
extern bool b_BootCompleted;
extern ScreenView currentScreen;
extern String espChipIdStr;

// Global Objects
extern AsyncWebServer webServer;
extern DNSServer dnsServer;
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern Preferences systemPreferences;

// Hardware Objects
extern Adafruit_SSD1306 oledDisplay;
extern Adafruit_ADS1115 ads;
extern USBCDC USBTelemetryPort;

// WiFi / AP
extern const IPAddress AP_LOCAL_IP;
extern const IPAddress AP_GATEWAY_IP;
extern const IPAddress AP_SUBNET_MASK;
extern const String AP_LOCAL_URL;

// Settings & Config
extern Settings systemSettings;
extern std::map<int, TemperatureSensorSettings> m_SensorSettings;
extern std::map<int, LedSettings> m_LedSettings;

// LED Data
extern CRGB a_LedBuffers[ACTIVE_LED_STRIPS][MAX_LEDS_PER_STRIP];

// Thermistor/Fan IDs
extern int a_ThermistorIds[ACTIVE_THERMISTORS];
extern int a_FanIds[ACTIVE_FANS];

// Fan State
extern unsigned long a_CurrentFanSpeedsRpm[ACTIVE_FANS];
extern std::map<int, FanRpmTarget> m_TargetFanRpm;

// Fan ISR Timestamps (MUST be volatile)
extern volatile unsigned long fan0_TS1, fan0_TS2;
extern volatile unsigned long fan1_TS1, fan1_TS2;
extern volatile unsigned long fan2_TS1, fan2_TS2;
extern volatile unsigned long fan3_TS1, fan3_TS2;

#endif // GLOBALS_H