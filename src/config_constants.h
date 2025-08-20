#ifndef CONFIG_CONSTANTS_H
#define CONFIG_CONSTANTS_H

// --- MQTT ---
constexpr int MQTT_DEFAULT_PORT = 1883;
constexpr int MQTT_CLIENT_BUFFER_SIZE = 10240;
constexpr int MQTT_SOCKET_TIMEOUT_SECS = 60;

// --- System Behaviour ---
constexpr bool FORMAT_FS_ON_FAIL = true;
constexpr bool DEBUG_ENABLED = true;
constexpr bool DEBUG_MQTT_ENABLED = true;
constexpr bool DEBUG_DATA_ENABLED = false;
constexpr unsigned long TELEMETRY_INTERVAL_MS = 30000;
constexpr bool CLEAR_PREFERENCES_ON_EVERY_BOOT = false;

// --- Screen ---
constexpr int SCREEN_WIDTH = 128; // OLED display width, in pixels
constexpr int SCREEN_HEIGHT = 64; // OLED display height, in pixels
constexpr uint8_t SCREEN_ADDR = 0x3C; // 0x3D for 128x64, 0x3C for 128x32

// --- Thermistor Control ---
constexpr int T_REFERENCE_RESISTANCE = 10000;
constexpr int T_NOMINAL_RESISTANCE = 10000;
constexpr int T_NOMINAL_TEMPERATURE = 25;
constexpr int T_B_VALUE = 3950;
constexpr int ESP32_ANALOG_RESOLUTION = 4095;
constexpr double ADC_VOLTAGE = 3.3;

// --- Fan Control ---
constexpr int FAN_DEBOUNCE_MS = 0; // Milliseconds
constexpr int FAN_STUCK_THRESHOLD_MD = 500; // Milliseconds
constexpr int PWM_RESOLUTION_BITS = 8;
constexpr int PWM_SIGNAL_FREQUENCY_HZ = 20000; // Hz

// --- LED Control ---
constexpr int ACTIVE_LED_STRIPS = 2;
constexpr int MAX_LEDS_PER_STRIP = 64;

// --- Hardware Counts ---
constexpr int ACTIVE_THERMISTORS = 2;
constexpr int ACTIVE_FANS = 4;

#endif // CONFIG_CONSTANTS_H