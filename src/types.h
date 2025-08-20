#ifndef TYPES_H
#define TYPES_H

#include <string>
#include <vector>
#include <Arduino.h> // For String

// --- Enums ---
enum class LedChannel { Internal, External };
enum class ScreenView { Overview, Temperatures, Fans, Rgb };

// --- Structs ---
struct Settings {
  
  String ssid;
  String password;
  String hostname;
  bool setup_done = false;
  bool offline_mode = true;
  int telemetry_interval = 30000;
  String units = "C";

  // MQTT settings
  bool mqtt_enable = false;
  String mqtt_broker = "broker.emqx.io";
  String mqtt_topic; // Will be set with ChipID
  String mqtt_username = "";
  String mqtt_password = "";
  int mqtt_port = 1883;
};

struct FanSpeedPoint {
  float temperature_threshold;
  int fan_duty_cycle;
};

struct TemperatureSensorSettings {
  String sensor_name;
  int temperature_alarm_threshold = 999;
  int rpm_alarm_threshold = -1;
  uint8_t step_duration_seconds = 1;
  std::vector<FanSpeedPoint> fan_speed_curve;
};

struct LedSettings {
  uint8_t prev_mode = 1;
  uint8_t mode = 1;
  uint8_t speed = 100;
  uint8_t num_leds = 32;
  uint32_t start_color = 0x00FF00;
  uint32_t end_color = 0xFF00FF;
};

struct FanRpmTarget {
  unsigned long start_time_ms = 0;
  int step_value = 0;
  int current_rpm = 0;
  int target_rpm = 0;
  bool is_adjusting = false;
};

struct FanPinPair {
  uint8_t tach_pin;
  uint8_t pwm_pin;
};

#endif // TYPES_H