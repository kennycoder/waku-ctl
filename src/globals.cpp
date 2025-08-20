#include "globals.h"


// --- Global Variable Definitions ---

// System State
unsigned long gHoldButtonCounter = 0;
bool b_TempAlarmFiring = false;
bool b_RpmAlarmFiring = false;
bool b_ResetPressed = false;
bool b_BootCompleted = false;
Preferences systemPreferences;
ScreenView currentScreen = ScreenView::Overview;
String espChipIdStr = "AA:BB:CC:DD:EE"; // Updated during init

// Hardware Objects
Adafruit_SSD1306 oledDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, PIN_OLED_RESET);
AsyncWebServer webServer(80);
DNSServer dnsServer;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_ADS1115 ads;
USBCDC USBTelemetryPort;

// WiFi / AP
const IPAddress AP_LOCAL_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET_MASK(255, 255, 255, 0);
const String AP_LOCAL_URL = "http://192.168.4.1";

// Settings & Config
Settings systemSettings;
std::map<int, TemperatureSensorSettings> m_SensorSettings;
std::map<int, LedSettings> m_LedSettings;

// LED Data
CRGB a_LedBuffers[ACTIVE_LED_STRIPS][MAX_LEDS_PER_STRIP];

// Thermistor/Fan IDs
int a_ThermistorIds[ACTIVE_THERMISTORS] = {0, 1};
int a_FanIds[ACTIVE_FANS] = {0, 1, 2, 3};

// Fan State
unsigned long a_CurrentFanSpeedsRpm[ACTIVE_FANS] = {999, 999, 999, 999};
std::map<int, FanRpmTarget> m_TargetFanRpm = {
    {0, {0, 0, 0, 0, false}},
    {1, {0, 0, 0, 0, false}},
    {2, {0, 0, 0, 0, false}},
    {3, {0, 0, 0, 0, false}}
};

std::map<int, FanPinPair> PIN_FAN_MAP = {
    {0, {14, 13}},
    {1, {12, 11}},
    {2, {10, 9}},
    {3, {7, 6}}
};

// Fan ISR Timestamps
volatile unsigned long fan0_TS1 = 0, fan0_TS2 = 0;
volatile unsigned long fan1_TS1 = 0, fan1_TS2 = 0;
volatile unsigned long fan2_TS1 = 0, fan2_TS2 = 0;
volatile unsigned long fan3_TS1 = 0, fan3_TS2 = 0;