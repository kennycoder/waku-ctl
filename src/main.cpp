#include "globals.h"
#include "led_manager.h"
#include "mqtt_manager.h"
#include "peripherals_manager.h"
#include "wifi_manager.h"
#include <ElegantOTA.h>
#include <PID_v1.h>
#include <TaskScheduler.h>
#include <Ticker.h>
#include <esp_wifi.h> // Used for mpdu_rx_disable android workaround

enum {
  TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP = 1u << 5,
  TUSB_DESC_CONFIG_ATT_SELF_POWERED = 1u << 6,
};

Task *gSendTelemetryTask = nullptr;
TaskHandle_t gProcessPIDControllerTaskHandle = NULL;
Scheduler taskScheduler;
Ticker tachTimer;

// --- Function Prototypes ---

// Initialization
void InitializeConfig();
void ClearPreferences();
void SaveConfig(const Settings &s);

void InitializeTasks();
void InitializeHttpServer();
void InitializeFanCurves();

// Core Logic
void setup();
void RunPostSetup();
void loop();

// Tasks
void MonitorButtonTask(void *pvParameters);
void MonitorStatesTask(void *pvParameters);
void ProcessTemperatureCurvesTask(void *pvParameters);
void ProcessPIDControllerTask(void *pvParameters);
void PlayLedsTask(void *pvParameters);
void GenerateTachSignalTask(void *pvParameters);
void DisplayDataTask(void *pvParameters);
void NativeUsbTelemetryTask(void *pvParameters);
void PlayAlarmsTask(void *pvParameters);

String GenerateSettingsJsonString(bool hidePassword = false);

// Telemetry
std::string PrepareTelemetryPayload(const std::string &event = "default");
String GenerateSensorsJson();

// HTTP Server
void HandleHttpNotFound(AsyncWebServerRequest *request);
void StreamFile(const String &path, const String &mimetype, bool cache,
                AsyncWebServerRequest *request);

// --- Initialization Functions ---

void InitializeConfig() {
  systemPreferences.begin("config", false);
  if (CLEAR_PREFERENCES_ON_EVERY_BOOT) {
    ClearPreferences();
  }

  uint64_t mac = ESP.getEfuseMac();
  char mac_str[18];
  sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)(mac >> 40),
          (uint8_t)(mac >> 32), (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
          (uint8_t)(mac >> 8), (uint8_t)(mac));

  espChipIdStr = mac_str;

  systemSettings.ssid = systemPreferences.getString("ssid", "");
  systemSettings.password = systemPreferences.getString("password", "");
  systemSettings.hostname =
      systemPreferences.getString("hostname", "waku-ctl.local");
  systemSettings.telemetry_interval =
      systemPreferences.getInt("tel_itv", TELEMETRY_INTERVAL_MS);
  systemSettings.setup_done = systemPreferences.getBool("setup_done", false);
  systemSettings.offline_mode = systemPreferences.getBool("offline_mode", true);
  systemSettings.units = systemPreferences.getString("units", "C");
  systemSettings.screen_rotation = systemPreferences.getUChar("screen_rot", 0);

  systemSettings.mqtt_broker =
      systemPreferences.getString("mqtt_broker", "broker.emqx.io");
  systemSettings.mqtt_enable = systemPreferences.getBool("mqtt_enable", false);
  systemSettings.mqtt_topic = systemPreferences.getString(
      "mqtt_topic", "waku-ctl/telemetry/" + espChipIdStr);
  systemSettings.mqtt_username =
      systemPreferences.getString("mqtt_username", "");
  systemSettings.mqtt_password =
      systemPreferences.getString("mqtt_password", "");
  systemSettings.mqtt_port =
      systemPreferences.getInt("mqtt_port", MQTT_DEFAULT_PORT);
  systemSettings.fan_passthrough =
      systemPreferences.getInt("fan_passthrough", 0);

  Serial.println("Settings loaded.");
}

void SaveConfig(const Settings &s) {
  systemPreferences.putString("ssid", s.ssid);
  systemPreferences.putString("password", s.password);
  systemPreferences.putString("units", s.units);
  systemPreferences.putInt("tel_itv", s.telemetry_interval);
  systemPreferences.putBool("setup_done", s.setup_done);
  systemPreferences.putBool("offline_mode", s.offline_mode);

  systemPreferences.putBool("mqtt_enable", s.mqtt_enable);
  systemPreferences.putString("mqtt_broker", s.mqtt_broker);
  systemPreferences.putString("mqtt_topic", s.mqtt_topic);
  systemPreferences.putString("mqtt_username", s.mqtt_username);
  systemPreferences.putString("mqtt_password", s.mqtt_password);
  systemPreferences.putInt("mqtt_port", s.mqtt_port);
  systemPreferences.putInt("fan_passthrough", s.fan_passthrough);
  systemPreferences.putUChar("screen_rot", s.screen_rotation);
  Serial.println("Settings saved.");
}

void InitializeTasks() {

  xTaskCreate(MonitorStatesTask, "MonitorStates", 4096, NULL, 8, NULL);
  xTaskCreate(ProcessTemperatureCurvesTask, "ReadTemps", 4096, NULL, 7, NULL);
  xTaskCreate(ProcessPIDControllerTask, "ProcessPIDControllerTask", 4096, NULL,
              6, &gProcessPIDControllerTaskHandle);
  xTaskCreate(PlayLedsTask, "PlayLEDs", 4096, NULL, 5, NULL);
  xTaskCreate(DisplayDataTask, "DisplayData", 4096, NULL, 3, NULL);
  xTaskCreate(NativeUsbTelemetryTask, "UsbTelTask", 4096, NULL, 2, NULL);
  xTaskCreate(PlayAlarmsTask, "PlayAlarms", 4096, NULL, tskIDLE_PRIORITY, NULL);

  InitializeMqttTelemetryTask(taskScheduler, gSendTelemetryTask);
  Serial.println("Tasks initialized.");
}

void InitializeFanCurves() {
  for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
    String fan_key = "FAN_" + String(fan_id);
    String fan_curves = systemPreferences.getString(fan_key.c_str(), "{}");
    JsonDocument fan_doc;

    Serial.printf("Loading %s - %s\n", fan_key.c_str(), fan_curves.c_str());

    DeserializationError error = deserializeJson(fan_doc, fan_curves);

    if (error || fan_curves == "{}" || fan_curves == "[]") {
      Serial.printf("No/Invalid settings for %s, using defaults.\n",
                    fan_key.c_str());
      m_SensorSettings[fan_id].sensor_id = 0;
      m_SensorSettings[fan_id].temperature_alarm_threshold = 999;
      m_SensorSettings[fan_id].rpm_alarm_threshold = -1;
      m_SensorSettings[fan_id].step_duration_seconds = 1;
      m_SensorSettings[fan_id].fan_speed_curve.push_back(
          {30.0f, MapFanPercentToPwm(30)});
      m_SensorSettings[fan_id].fan_speed_curve.push_back(
          {33.0f, MapFanPercentToPwm(40)});
      m_SensorSettings[fan_id].fan_speed_curve.push_back(
          {36.0f, MapFanPercentToPwm(55)});
      m_SensorSettings[fan_id].fan_speed_curve.push_back(
          {39.0f, MapFanPercentToPwm(75)});
      m_SensorSettings[fan_id].fan_speed_curve.push_back(
          {41.0f, MapFanPercentToPwm(100)});
      m_SensorSettings[fan_id].mode = 0;
      m_SensorSettings[fan_id].pid_kp = 2;
      m_SensorSettings[fan_id].pid_ki = 5;
      m_SensorSettings[fan_id].pid_kd = 1;
      m_SensorSettings[fan_id].min_duty = 51;
      m_SensorSettings[fan_id].max_duty = 255;

      fan_doc["sensor"] = m_SensorSettings[fan_id].sensor_id;
      fan_doc["curves"] = fan_doc.to<JsonArray>();
      for (const auto &setting : m_SensorSettings[fan_id].fan_speed_curve) {
        JsonObject curve_point = fan_doc["curves"].add<JsonObject>();
        curve_point["temp"] = setting.temperature_threshold;
        curve_point["fan"] = setting.fan_duty_cycle;
      }
      fan_doc["temp_th"] = m_SensorSettings[fan_id].temperature_alarm_threshold;
      fan_doc["duty_th"] = m_SensorSettings[fan_id].rpm_alarm_threshold;
      fan_doc["sud_dur"] = m_SensorSettings[fan_id].step_duration_seconds;
      fan_doc["halt_on"] = m_SensorSettings[fan_id].halt_on;
      fan_doc["mode"] = m_SensorSettings[fan_id].mode;
      fan_doc["pid_kp"] = m_SensorSettings[fan_id].pid_kp;
      fan_doc["pid_ki"] = m_SensorSettings[fan_id].pid_ki;
      fan_doc["pid_kd"] = m_SensorSettings[fan_id].pid_kd;
      fan_doc["pid_setpoint"] = m_SensorSettings[fan_id].pid_setpoint;
      fan_doc["min_duty"] = m_SensorSettings[fan_id].min_duty;
      fan_doc["max_duty"] = m_SensorSettings[fan_id].max_duty;

      String settings_json;
      serializeJson(fan_doc, settings_json);
      systemPreferences.putString(fan_key.c_str(), settings_json);
    } else {
      m_SensorSettings[fan_id].sensor_id = fan_doc["sensor"].as<int>();
      m_SensorSettings[fan_id].temperature_alarm_threshold =
          fan_doc["temp_th"].as<int>();
      m_SensorSettings[fan_id].rpm_alarm_threshold =
          fan_doc["duty_th"].as<int>();
      m_SensorSettings[fan_id].step_duration_seconds =
          fan_doc["sud_dur"].as<uint8_t>();
      m_SensorSettings[fan_id].halt_on = fan_doc["halt_on"].as<uint8_t>();
      m_SensorSettings[fan_id].mode = fan_doc["mode"].as<uint8_t>();
      m_SensorSettings[fan_id].pid_kp = fan_doc["pid_kp"].as<double>();
      m_SensorSettings[fan_id].pid_ki = fan_doc["pid_ki"].as<double>();
      m_SensorSettings[fan_id].pid_kd = fan_doc["pid_kd"].as<double>();
      m_SensorSettings[fan_id].pid_setpoint =
          fan_doc["pid_setpoint"].as<double>();
      if (fan_doc["min_duty"].is<uint8_t>()) {
        m_SensorSettings[fan_id].min_duty = fan_doc["min_duty"].as<uint8_t>();
      } else {
        m_SensorSettings[fan_id].min_duty = 51;
      }
      if (fan_doc["max_duty"].is<uint8_t>()) {
        m_SensorSettings[fan_id].max_duty = fan_doc["max_duty"].as<uint8_t>();
      } else {
        m_SensorSettings[fan_id].max_duty = 255;
      }

      m_SensorSettings[fan_id].fan_speed_curve.clear();
      for (auto const &setting : fan_doc["curves"].as<JsonArray>()) {
        m_SensorSettings[fan_id].fan_speed_curve.push_back(
            {setting["temp"].as<float>(), setting["fan"].as<int>()});
      }
    }
  }

  // Set initial fan speeds based on current temps
  for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
    const double temp = 25;
    const int target_speed =
        (temp > 0) ? CalculateFanSpeed(fan_id, temp) : MapFanPercentToPwm(25);

    m_TargetFanRpm[fan_id].current_rpm = target_speed;
    m_TargetFanRpm[fan_id].target_rpm = target_speed;
    ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target_speed);
  }
}

// --- Core Logic & Tasks ---

void setup() {

  init_globals();
  USB.PID(0x82E5);
  USB.VID(0x303A);
  USB.productName("WaKu Controller");
  USB.manufacturerName("kenny's Labs");
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_SELF_POWERED |
                    TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
  USBTelemetryPort.begin(115200);
  USB.begin();

  Serial.begin(115200);
  delay(1000); // Wait for serial
  Serial.println("--- WaKu-ctl Booting ---");

  if (!LittleFS.begin(FORMAT_FS_ON_FAIL)) {
    Serial.println("LittleFS Mount Failed!");
    return;
  }
  Serial.println("LittleFS Mounted.");

  Wire.setPins(PIN_SDA, PIN_SCL);
  Wire.begin();

  // Reset button task needs to be started earlier
  xTaskCreate(MonitorButtonTask, "MonitorButton", 4096, NULL, 1, NULL);
  // Same goes for signal passthrough
  xTaskCreate(GenerateTachSignalTask, "GenerateTachSignalTask", 4096, NULL, 4,
              NULL);

  InitializeConfig();
  InitializeOutputs();
  InitializeInputs();
  InitializeScreen();
  InitializeWifi();
  InitializeHttpServer();

  if (!systemSettings.setup_done) {
    Serial.println(
        "Setup not complete. Waiting for configuration via AP/Web Server.");

    oledDisplay.clearDisplay();
    oledDisplay.setCursor(0, 0);
    oledDisplay.printf("   ### SETUP ###\n\n");
    oledDisplay.printf("SSID: WaKu-ctl\nIP: %s",
                       AP_LOCAL_IP.toString().c_str());
    oledDisplay.display();

    return; // Wait for user to complete setup via web
  }

  RunPostSetup();
  Serial.println("--- WaKu-ctl Ready ---");
}

void RunPostSetup() {
  Serial.println("Running Post-Setup...");
  InitializeMqttClient();
  // InitializeNtpTime(); // Optional, not used for now
  InitializeAdc();
  InitializeFanCurves();
  InitializeLeds();
  InitializeTasks();
  b_BootCompleted = true;
  Serial.println("Post-Setup Complete.");
}

void loop() {
  if (b_BootCompleted) {
    taskScheduler.execute();
    LoopMqttClient();

    int t_id = 0;
    for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
      // IMPORTANT: This is a workaround for a hardware layout where T0 and T1
      // thermistors are swapped on the board.
      // TODO: Fix hardware design in future revisions to avoid this workaround.
      t_id = i;
      if (i == 0)
        t_id = 1;
      else if (i == 1)
        t_id = 0;

      a_currentTemperatures[t_id] = ReadTemperature(i);
    }

    if (ACTIVE_THERMISTORS > 1) {
      int k = 0;
      for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
        for (int j = i + 1; j < ACTIVE_THERMISTORS; ++j) {
          a_currentTemperatures[ACTIVE_THERMISTORS + k] =
              a_currentTemperatures[j] - a_currentTemperatures[i];
          if (abs(a_currentTemperatures[ACTIVE_THERMISTORS + k]) ==
                  a_currentTemperatures[j] ||
              abs(a_currentTemperatures[ACTIVE_THERMISTORS + k]) ==
                  a_currentTemperatures[i]) {
            a_currentTemperatures[ACTIVE_THERMISTORS + k] = 0;
          }
          // Serial.printf("Delta T%d_T%d: %.2f C\n", a_ThermistorIds[i]+1,
          // a_ThermistorIds[j]+1, a_currentTemperatures[ACTIVE_THERMISTORS +
          // k]);
          k++;
        }
      }
    }

    if (ACTIVE_FANS > 0) {
      for (int fan_id = 0; fan_id < ACTIVE_FANS; ++fan_id) {
        a_CurrentFanSpeedsRpm[fan_id] = ReadFanRpm(fan_id);
      }
    }

    //  Serial.printf("------------\n");
  }

  ElegantOTA.loop();
  vTaskDelay(pdMS_TO_TICKS(250)); // Yield, let tasks run
}

void MonitorButtonTask(void *pvParameters) {
  while (true) {
    // --- Monitor Reset Button ---
    if (digitalRead(PIN_RESET_SETTINGS) == LOW) {
      if (!b_ResetPressed) {
        b_ResetPressed = true;
        gHoldButtonCounter = millis();
        Serial.println("Reset button pressed.");
      } else if (millis() - gHoldButtonCounter >= 5000) {
        // If setup is completed, this button is used to do factory reset
        if (systemSettings.setup_done) {
          Serial.println("Holding > 5s. Clearing preferences & rebooting!");
          ClearPreferences();
          delay(1000);
          esp_restart();
        } else { // Otherwise it's a hack for quick setup
          systemSettings.offline_mode = true;
          systemSettings.ssid = "";
          systemSettings.password = "";
          systemSettings.hostname = "waku-ctl.local";
          systemSettings.setup_done = true;
          systemSettings.telemetry_interval = 30000;
          systemSettings.units = "C";
          b_ResetPressed = false;

          SaveConfig(systemSettings);
          RunPostSetup();
        }
      }
    } else {
      if (b_ResetPressed) { // Button was pressed and now released
        b_ResetPressed = false;
        gHoldButtonCounter = 0;
        if (b_BootCompleted) {
          Serial.println("Reset button released. Cycling screen.");
          // Cycle through screens
          int current_view_int = static_cast<int>(currentScreen);
          current_view_int = (current_view_int + 1) % 4; // 4 screens total
          currentScreen = static_cast<ScreenView>(current_view_int);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Check ~4 times a second
  }
}

void MonitorStatesTask(void *pvParameters) {
  while (true) {
    // --- Monitor Alarms ---
    bool temp_alarm_active = false;
    bool rpm_alarm_active = false;

    for (int fan_id = 0; fan_id < ACTIVE_FANS; ++fan_id) {
      const auto &settings = m_SensorSettings[fan_id];

      const double temp = a_currentTemperatures[settings.sensor_id];
      const unsigned long current_rpm = a_CurrentFanSpeedsRpm[fan_id];

      // Temperature Alarm
      if (temp > 0 && settings.temperature_alarm_threshold > 0 &&
          temp >= settings.temperature_alarm_threshold) {
        if (settings.halt_on == HALT_ON_ALARM_TEMP ||
            settings.halt_on == HALT_ON_ALARM_BOTH) {
          Serial.println("Halting system due to alarm condition.");
          digitalWrite(PIN_PWR, HIGH); // Cut power
        }
        temp_alarm_active = true;
        if (!b_TempAlarmFiring)
          Serial.printf("ALARM: Temp high on %s (%.1fC)\n", settings.sensor_id,
                        temp);
      }

      // RPM Alarm (only if threshold is set, > 0)
      if (settings.rpm_alarm_threshold >= 0 &&
          current_rpm < (unsigned long)settings.rpm_alarm_threshold) {
        if (settings.halt_on == HALT_ON_ALARM_FAN ||
            settings.halt_on == HALT_ON_ALARM_BOTH) {
          Serial.println("Halting system due to alarm condition.");
          digitalWrite(PIN_PWR, HIGH); // Cut power
        }
        rpm_alarm_active = true;
        if (!b_RpmAlarmFiring)
          Serial.printf("ALARM: RPM low on FAN_%d (%lu RPM)\n", fan_id,
                        current_rpm);
      }
    }

    b_TempAlarmFiring = temp_alarm_active;
    b_RpmAlarmFiring = rpm_alarm_active;

    vTaskDelay(pdMS_TO_TICKS(250)); // Check ~4 times a second
  }
}

void PlayAlarmsTask(void *pvParameters) {
  while (true) {
    if (b_TempAlarmFiring || b_RpmAlarmFiring) {
      // Play sound (Beep pattern)
      tone(PIN_BUZZER, b_TempAlarmFiring ? 1000 : 4000, 500);
      vTaskDelay(pdMS_TO_TICKS(1000));
      b_TempAlarmStopped = false;
    } else {
      if (!b_TempAlarmStopped) {
        noTone(PIN_BUZZER);
        digitalWrite(PIN_PWR,
                     LOW); // Stop cutting power (if cutting), alarm turned off.
        b_TempAlarmStopped = true;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }
}

void ProcessTemperatureCurvesTask(void *pvParameters) {
  while (true) {

    if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
      for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
        Serial.printf("T%d: %.2f C; ", i + 1, a_currentTemperatures[i]);
      }
      Serial.printf("\n");
    }

    for (int fan_id = 0; fan_id < ACTIVE_FANS; ++fan_id) {

      if (m_SensorSettings[fan_id].mode == 1) { // Only run if in curve mode
        continue;                               // Skip to next fan
      }

      const auto &settings = m_SensorSettings[fan_id];
      const double temp = a_currentTemperatures[settings.sensor_id];

      if (temp <= 0) {
        if (DEBUG_ENABLED && DEBUG_DATA_ENABLED)
          Serial.printf("Temp sensor N/A for FAN_%d. Skipping.\n", fan_id);
        continue; // Skip if temp sensor not working/connected
      }

      auto &target = m_TargetFanRpm[fan_id];
      int new_target_pwm = CalculateFanSpeed(fan_id, temp);

      if (new_target_pwm != target.target_rpm && !target.is_adjusting) {
        target.target_rpm = new_target_pwm;
        target.step_value = (target.target_rpm - target.current_rpm) /
                            settings.step_duration_seconds;
        if (target.step_value == 0 && target.target_rpm != target.current_rpm) {
          target.step_value = (target.target_rpm > target.current_rpm) ? 1 : -1;
        }

        if (target.step_value != 0) {
          target.start_time_ms = millis();
          target.is_adjusting = true;
          Serial.printf("FAN_%d: Adjusting %d -> %d (Step: %d)\n", fan_id,
                        target.current_rpm, target.target_rpm,
                        target.step_value);
        } else {
          target.current_rpm = target.target_rpm; // No change needed
        }
      }

      if (target.is_adjusting && (millis() - target.start_time_ms >= 1000)) {
        target.start_time_ms = millis();
        target.current_rpm += target.step_value;

        // Clamp and check if target reached
        bool reached = false;
        if (target.step_value > 0 && target.current_rpm >= target.target_rpm) {
          target.current_rpm = target.target_rpm;
          reached = true;
        } else if (target.step_value < 0 &&
                   target.current_rpm <= target.target_rpm) {
          target.current_rpm = target.target_rpm;
          reached = true;
        }

        if (reached) {
          Serial.printf("FAN_%d: Reached target %d\n", fan_id,
                        target.target_rpm);
          target.is_adjusting = false;
        }
        if (m_CurrentFanPwmValues[fan_id] != target.current_rpm) {
          ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target.current_rpm);
          m_CurrentFanPwmValues[fan_id] = target.current_rpm;
        }

      } else if (!target.is_adjusting) {
        // Ensure it stays at target if not adjusting
        if (m_CurrentFanPwmValues[fan_id] != target.target_rpm) {
          ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target.target_rpm);
          m_CurrentFanPwmValues[fan_id] = target.target_rpm;
        }
      }

      if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
        Serial.printf("FAN_%d RPM: %lu (Target PWM: %d, Current PWM: %d)\n",
                      fan_id, a_CurrentFanSpeedsRpm[fan_id], target.target_rpm,
                      target.current_rpm);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250)); // Read temps/adjust fans twice a second
  }
}

void ProcessPIDControllerTask(void *pvParameters) {
  std::map<int, PID *> pidControllers;
  std::map<int, double *> pidSetpoints;

  for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
    if (m_SensorSettings[fan_id].mode == 1) {
      const auto &settings = m_SensorSettings[fan_id];
      double *input = &a_currentTemperatures[settings.sensor_id];

      m_PidOutputs[fan_id] = 0;
      double *output = &m_PidOutputs[fan_id];

      pidSetpoints[fan_id] = new double;
      *pidSetpoints[fan_id] = settings.pid_setpoint;

      pidControllers[fan_id] =
          new PID(input, output, pidSetpoints[fan_id], settings.pid_kp,
                  settings.pid_ki, settings.pid_kd, REVERSE);
      pidControllers[fan_id]->SetMode(AUTOMATIC);
      // Use configured minimum and maximum duty cycle
      pidControllers[fan_id]->SetOutputLimits(settings.min_duty, settings.max_duty);
    }
  }

  // Task-specific destructor
  auto cleanup = [&]() {
    for (auto &pair : pidControllers) {
      delete pair.second; // Free PID object
    }
    pidControllers.clear();

    for (auto &pair : pidSetpoints) {
      delete pair.second; // Free setpoint value
    }
    pidSetpoints.clear();
  };

  while (true) {
    // Check if the task has been requested to terminate
    if (eTaskGetState(gProcessPIDControllerTaskHandle) == eDeleted) {
      cleanup();
      vTaskDelete(NULL); // Delete self
    }

    for (auto &pair : pidControllers) {
      int fan_id = pair.first;
      PID *controller = pair.second;
      controller->Compute();
      int pwm_val = (int)m_PidOutputs[fan_id];
      if (m_CurrentFanPwmValues[fan_id] != pwm_val) {
        if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
          Serial.printf("Fan %d setting speed: %d\n", fan_id, pwm_val);
        }
        ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, pwm_val);
        m_CurrentFanPwmValues[fan_id] = pwm_val;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void PlayLedsTask(void *pvParameters) {
  while (true) {
    for (int i = 0; i < ACTIVE_LED_STRIPS; ++i) {
      PlayLedEffect(i);
    }
    vTaskDelay(pdMS_TO_TICKS(66)); // ~15 FPS target
  }
}

bool pinState = LOW;
void IRAM_ATTR onTachTick() {
  pinState = !pinState;
  digitalWrite(PIN_TACH, pinState);
}

void GenerateTachSignalTask(void *pvParameters) {
  bool is_timer_active = false;
  unsigned long last_rpm = 0;

  while (true) {
    if (systemSettings.fan_passthrough >= 0 &&
        systemSettings.fan_passthrough < ACTIVE_FANS) {
      unsigned long rpm = a_CurrentFanSpeedsRpm[systemSettings.fan_passthrough];

      if (rpm <= 0) {
        if (is_timer_active) {
          tachTimer.detach();
          is_timer_active = false;
          digitalWrite(PIN_TACH, HIGH); // Idle state
        }
        last_rpm = 0;
      } else {
        // RPM is > 0. Check for significant change.
        float difference =
            (last_rpm > 0) ? fabsf(1.0f - (float)rpm / last_rpm) : 1.0f;

        if (difference > 0.05f) {
          // Frequency = (RPM * PPR) / 60. PPR is 2 for fans.
          // Interval (sec) = 1 / (Frequency * 2) for toggle ISR
          float frequency = (rpm * 2) / 60.0f;
          float intervalSec = 0.5f / frequency;

          if (is_timer_active) {
            tachTimer.detach();
          }
          tachTimer.attach(intervalSec, onTachTick);
          is_timer_active = true;
          last_rpm = rpm;
        }
      }
    } else {
      // Passthrough is disabled, ensure timer is off
      if (is_timer_active) {
        tachTimer.detach();
        is_timer_active = false;
        digitalWrite(PIN_TACH, HIGH);
      }
      last_rpm = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // Update frequency twice a second
  }
}

void DisplayDataTask(void *pvParameters) {
  while (true) {
    oledDisplay.clearDisplay();
    oledDisplay.setCursor(0, 0);

    switch (currentScreen) {
    case ScreenView::Overview:
      oledDisplay.printf("   ### OVERVIEW ###\n\n");
      oledDisplay.printf("Mode: %s\n",
                         systemSettings.offline_mode ? "Offline" : "Connected");
      oledDisplay.printf("IP: %s\n", systemSettings.offline_mode
                                         ? AP_LOCAL_IP.toString().c_str()
                                         : WiFi.localIP().toString().c_str());
      oledDisplay.printf("Units: %s\n", systemSettings.units.c_str());
      oledDisplay.printf("Temp Alarm: %s\n", b_TempAlarmFiring ? "Yes" : "No");
      oledDisplay.printf("RPM Alarm: %s\n", b_RpmAlarmFiring ? "Yes" : "No");
      oledDisplay.setCursor(50, 56);
      oledDisplay.printf("o...");
      break;

    case ScreenView::Temperatures: {
      oledDisplay.printf(" ### TEMPERATURE ###\n\n");

      for (int i = 0; i < ACTIVE_THERMISTORS; i++) {
        double t = a_currentTemperatures[i];

        if (systemSettings.units == "F") {
          if (t > -90.0)
            t = (t * 1.8) + 32;
        }
        String print_t = ((t < 0 && systemSettings.units == "C") ||
                          (t < 32 && systemSettings.units == "F") || isnan(t))
                             ? "N/A"
                             : (String(t, 1) + systemSettings.units);
        Serial.printf("TEMP_%d: %s\n", (i + 1), print_t);
        oledDisplay.printf("TEMP_%d: %s\n", (i + 1), print_t);
      }

      oledDisplay.setCursor(50, 56);
      oledDisplay.printf(".o..");
    } break;

    case ScreenView::Fans:
      oledDisplay.printf("  ### FAN SPEED ### \n\n");
      for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
        oledDisplay.printf("FAN_%s: %4lu RPM\n", a_FanNames[fan_id],
                           a_CurrentFanSpeedsRpm[fan_id]);
      }
      oledDisplay.setCursor(50, 56);
      oledDisplay.printf("..o.");
      break;

    case ScreenView::Rgb:
      oledDisplay.printf("  ### RGB MODE ### \n\n");
      for (const auto &[key, value] : m_LedSettings) {
        std::string fkey = "LED_" + std::to_string(key);
        std::string led_mode = "Unknown";
        switch (value.mode) {
        case 0:
          led_mode = "Off";
          break;
        case 1:
          led_mode = "Static";
          break;
        case 2:
          led_mode = "Grad/Wave";
          break;
        case 3:
          led_mode = "Grad/Move";
          break;
        case 4:
          led_mode = "Rainbow";
          break;
        case 5:
          led_mode = "Passthrough";
          break;
        }
        oledDisplay.printf("%s: %s\n", fkey.c_str(), led_mode.c_str());
      }
      oledDisplay.setCursor(50, 56);
      oledDisplay.printf("...o");
      break;
    }

    oledDisplay.display();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Update display once a second
  }
}

void NativeUsbTelemetryTask(void *pvParameters);

// --- Serial Command Helpers ---

String GenerateFanCurvesJsonString() {
  JsonDocument doc;
  for (const auto &[key, value] : m_SensorSettings) {
    String fkey = "FAN_" + String(key);
    doc[fkey]["sensor"] = value.sensor_id;
    doc[fkey]["temp_th"] = value.temperature_alarm_threshold;
    doc[fkey]["duty_th"] = value.rpm_alarm_threshold;
    doc[fkey]["sud_dur"] = value.step_duration_seconds;
    doc[fkey]["halt_on"] = value.halt_on;
    doc[fkey]["units"] = systemSettings.units;
    doc[fkey]["mode"] = value.mode;
    doc[fkey]["pid_kp"] = value.pid_kp;
    doc[fkey]["pid_ki"] = value.pid_ki;
    doc[fkey]["pid_kd"] = value.pid_kd;
    doc[fkey]["pid_setpoint"] = value.pid_setpoint;
    doc[fkey]["min_duty"] = value.min_duty;
    doc[fkey]["max_duty"] = value.max_duty;
    JsonArray curves = doc[fkey]["curves"].to<JsonArray>();
    for (const auto &setting : value.fan_speed_curve) {
      JsonObject point = curves.add<JsonObject>();
      point["temp"] = setting.temperature_threshold;
      point["fan"] = setting.fan_duty_cycle;
    }
  }
  String buffer;
  serializeJson(doc, buffer);
  return buffer;
}

void SaveFanCurvesFromJson(const JsonVariant &json) {
  JsonObject root = json.as<JsonObject>();
  for (JsonPair kv : root) {
    String fan_name = kv.key().c_str(); // FAN_X
    int fan_id = fan_name.substring(4).toInt();
    JsonVariant fan_data = kv.value();

    if (m_SensorSettings.count(fan_id)) {
      String settings_str;
      serializeJson(fan_data, settings_str);
      Serial.printf("Saving %s via Serial\n", fan_name.c_str());
      systemPreferences.putString(fan_name.c_str(), settings_str);

      m_SensorSettings[fan_id].sensor_id = fan_data["sensor"].as<int>();
      m_SensorSettings[fan_id].temperature_alarm_threshold =
          fan_data["temp_th"].as<int>();
      m_SensorSettings[fan_id].rpm_alarm_threshold =
          fan_data["duty_th"].as<int>();
      m_SensorSettings[fan_id].step_duration_seconds =
          fan_data["sud_dur"].as<uint8_t>();
      m_SensorSettings[fan_id].halt_on = fan_data["halt_on"].as<uint8_t>();
      m_SensorSettings[fan_id].mode = fan_data["mode"].as<uint8_t>();
      m_SensorSettings[fan_id].pid_kp = fan_data["pid_kp"].as<double>();
      m_SensorSettings[fan_id].pid_ki = fan_data["pid_ki"].as<double>();
      m_SensorSettings[fan_id].pid_kd = fan_data["pid_kd"].as<double>();
      m_SensorSettings[fan_id].pid_setpoint =
          fan_data["pid_setpoint"].as<double>();
      if (fan_data["min_duty"].is<uint8_t>()) {
        m_SensorSettings[fan_id].min_duty = fan_data["min_duty"].as<uint8_t>();
      } else {
        m_SensorSettings[fan_id].min_duty = 51;
      }
      if (fan_data["max_duty"].is<uint8_t>()) {
        m_SensorSettings[fan_id].max_duty = fan_data["max_duty"].as<uint8_t>();
      } else {
        m_SensorSettings[fan_id].max_duty = 255;
      }
      m_SensorSettings[fan_id].fan_speed_curve.clear();
      for (const auto &setting : fan_data["curves"].as<JsonArray>()) {
        m_SensorSettings[fan_id].fan_speed_curve.push_back(
            {setting["temp"].as<float>(), setting["fan"].as<int>()});
      }
    }
  }
  // Restart PID task
  if (gProcessPIDControllerTaskHandle != NULL) {
    vTaskDelete(gProcessPIDControllerTaskHandle);
    gProcessPIDControllerTaskHandle = NULL;
  }
  xTaskCreate(ProcessPIDControllerTask, "ProcessPIDControllerTask", 4096, NULL,
              5, &gProcessPIDControllerTaskHandle);
}

String GenerateSettingsJsonString(bool hidePassword) {
  JsonDocument doc;
  doc["ssid"] = systemSettings.ssid;
  doc["password"] = hidePassword ? "<redacted>" : systemSettings.password;
  doc["hostname"] = systemSettings.hostname;
  doc["tel_itv"] = systemSettings.telemetry_interval;
  doc["setup_done"] = systemSettings.setup_done;
  doc["offline_mode"] = systemSettings.offline_mode;
  doc["units"] = systemSettings.units;
  doc["mqtt_broker"] = systemSettings.mqtt_broker;
  doc["mqtt_topic"] = systemSettings.mqtt_topic;
  doc["mqtt_enable"] = systemSettings.mqtt_enable;
  doc["mqtt_username"] = systemSettings.mqtt_username;
  doc["mqtt_password"] = systemSettings.mqtt_password;
  doc["mqtt_port"] = systemSettings.mqtt_port;
  doc["fan_passthrough"] = systemSettings.fan_passthrough;
  doc["screen_rotation"] = systemSettings.screen_rotation;
  String buffer;
  serializeJson(doc, buffer);
  return buffer;
}

void SaveSettingsFromJson(const JsonVariant &json) {
  JsonObject root = json.as<JsonObject>();

  // Check for changes requiring reboot
  bool needs_reboot = false;
  if (root["offline_mode"].is<bool>()) {
    bool new_offline = root["offline_mode"];
    if (systemSettings.offline_mode != new_offline)
      needs_reboot = true;
    systemSettings.offline_mode = new_offline;
  }

  if (root["ssid"].is<const char *>()) {
    String new_ssid = root["ssid"].as<String>();
    if (systemSettings.ssid != new_ssid)
      needs_reboot = true;
    systemSettings.ssid = new_ssid;
  }
  if (root["password"].is<const char *>()) {
    String new_password = root["password"].as<String>();
    if (systemSettings.password != new_password)
      needs_reboot = true;
    systemSettings.password = new_password;
  }
  if (root["hostname"].is<const char *>())
    systemSettings.hostname = root["hostname"].as<String>();
  systemSettings.setup_done = true;

  if (root["tel_itv"].is<int>())
    systemSettings.telemetry_interval = root["tel_itv"];
  if (root["units"].is<const char *>())
    systemSettings.units = root["units"].as<String>();
  if (root["mqtt_enable"].is<bool>())
    systemSettings.mqtt_enable = root["mqtt_enable"];
  if (root["mqtt_username"].is<const char *>())
    systemSettings.mqtt_username = root["mqtt_username"].as<String>();
  if (root["mqtt_password"].is<const char *>())
    systemSettings.mqtt_password = root["mqtt_password"].as<String>();
  if (root["mqtt_topic"].is<const char *>())
    systemSettings.mqtt_topic = root["mqtt_topic"].as<String>();
  if (root["mqtt_broker"].is<const char *>())
    systemSettings.mqtt_broker = root["mqtt_broker"].as<String>();
  if (root["mqtt_port"].is<int>())
    systemSettings.mqtt_port = root["mqtt_port"];
  if (root["fan_passthrough"].is<int>())
    systemSettings.fan_passthrough = root["fan_passthrough"];
  if (root["screen_rotation"].is<int>())
    systemSettings.screen_rotation = root["screen_rotation"];

  SaveConfig(systemSettings);

  if (needs_reboot ||
      (root["force_reboot"].is<bool>() && root["force_reboot"].as<bool>())) {
    Serial.println("Settings saved via Serial, rebooting...");
    delay(500);
    esp_restart();
  }
}

String GenerateRgbJsonString() {
  JsonDocument doc;
  for (const auto &[key, value] : m_LedSettings) {
    String fkey = "LED_" + String(key);
    doc[fkey]["mode"] = value.mode;
    doc[fkey]["speed"] = value.speed;
    doc[fkey]["start_color"] = value.start_color;
    doc[fkey]["end_color"] = value.end_color;
    doc[fkey]["num_leds"] = value.num_leds;
  }
  String buffer;
  serializeJson(doc, buffer);
  return buffer;
}

void SaveRgbFromJson(const JsonVariant &json) {
  JsonObject root = json.as<JsonObject>();
  for (JsonPair kv : root) {
    String led_name = kv.key().c_str(); // LED_X
    int led_index = led_name.substring(4).toInt();
    JsonVariant led_data = kv.value();

    if (m_LedSettings.count(led_index)) {
      String led_str;
      serializeJson(led_data, led_str);
      Serial.printf("Saving %s via Serial\n", led_name.c_str());
      systemPreferences.putString(led_name.c_str(), led_str);

      m_LedSettings[led_index].prev_mode = m_LedSettings[led_index].mode;
      m_LedSettings[led_index].mode = led_data["mode"];
      m_LedSettings[led_index].speed = led_data["speed"];
      m_LedSettings[led_index].start_color = led_data["start_color"];
      m_LedSettings[led_index].end_color = led_data["end_color"];
      m_LedSettings[led_index].num_leds = led_data["num_leds"];
    }
  }
}

String GenerateBackupJsonString() {
  String json = "{";
  json += "\"settings\":" + GenerateSettingsJsonString() + ",";
  json += "\"fans\":" + GenerateFanCurvesJsonString() + ",";
  json += "\"rgb\":" + GenerateRgbJsonString();
  json += "}";
  return json;
}

void RestoreBackupFromJson(const JsonVariant &json) {
  if (json["settings"].is<JsonObject>()) {
    SaveSettingsFromJson(json["settings"]);
  }
  if (json["fans"].is<JsonObject>()) {
    SaveFanCurvesFromJson(json["fans"]);
  }
  if (json["rgb"].is<JsonObject>()) {
    SaveRgbFromJson(json["rgb"]);
  }
}

void NativeUsbTelemetryTask(void *pvParameters) {
  unsigned long last_telemetry_time = 0;
  const unsigned long telemetry_interval =
      1000; // Send every 1 second by default

  // Buffer for incoming serial data
  String serialBuffer = "";

  while (true) {
    if (USBTelemetryPort) {
      // 1. Handle Incoming Commands
      while (USBTelemetryPort.available()) {
        char c = USBTelemetryPort.read();

        if (c == '\n') {
          // Process command
          serialBuffer.trim();
          if (serialBuffer.length() > 0) {
            int spaceIdx = serialBuffer.indexOf(' ');
            String command = (spaceIdx > 0)
                                 ? serialBuffer.substring(0, spaceIdx)
                                 : serialBuffer;
            String payload =
                (spaceIdx > 0) ? serialBuffer.substring(spaceIdx + 1) : "{}";

            // Parse JSON payload if any
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            Serial.printf("Command: %s, Payload: %s\n", command.c_str(),
                          payload.c_str());

            if (command == "get-sensors") {
              USBTelemetryPort.println(GenerateSensorsJson());
            } else if (command == "get-data") {
              USBTelemetryPort.println(
                  PrepareTelemetryPayload("manual_fetch").c_str());
            } else if (command == "get-curves") {
              USBTelemetryPort.println(GenerateFanCurvesJsonString());
            } else if (command == "save-curves") {
              if (!error) {
                SaveFanCurvesFromJson(doc);
                USBTelemetryPort.println("{\"status\": \"curves_saved\"}");
              } else {
                USBTelemetryPort.println("{\"error\": \"invalid_json\"}");
              }
            } else if (command == "get-settings") {
              USBTelemetryPort.println(GenerateSettingsJsonString());
            } else if (command == "save-settings") {
              if (!error) {
                USBTelemetryPort.println("{\"status\": \"settings_saved\"}");
                SaveSettingsFromJson(doc);
                // Note: Reboot might happen in function
              } else {
                USBTelemetryPort.println("{\"error\": \"invalid_json\"}");
              }
            } else if (command == "clear-settings") {
              ClearPreferences();
              USBTelemetryPort.println("{\"status\": \"cleared_restarting\"}");
              delay(500);
              esp_restart();
            } else if (command == "get-rgb") {
              USBTelemetryPort.println(GenerateRgbJsonString());
            } else if (command == "save-rgb") {
              if (!error) {
                SaveRgbFromJson(doc);
                USBTelemetryPort.println("{\"status\": \"rgb_saved\"}");
              } else {
                USBTelemetryPort.println("{\"error\": \"invalid_json\"}");
              }
            } else if (command == "backup") {
              USBTelemetryPort.println(GenerateBackupJsonString());
            } else if (command == "restore") {
              if (!error) {
                RestoreBackupFromJson(doc);
                USBTelemetryPort.println(
                    "{\"status\": \"restored_restarting\"}");
                delay(500);
                esp_restart();
              } else {
                USBTelemetryPort.println("{\"error\": \"invalid_json\"}");
              }
            } else if (command == "networks") {
              JsonDocument netDoc;
              ScanWifiNetworks(netDoc);
              String netBuf;
              serializeJson(netDoc, netBuf);
              USBTelemetryPort.println(netBuf);
            } else {
              USBTelemetryPort.println("{\"error\": \"unknown_command\"}");
            }
          }
          serialBuffer = "";
        } else {
          serialBuffer += c;
        }
      }

      // 2. Handle Periodic Telemetry
      unsigned long now = millis();
      if (now - last_telemetry_time >= telemetry_interval) {
        last_telemetry_time = now;
        std::string payload = PrepareTelemetryPayload("usb_stream");

        // Serial.printf("USB Telemetry: %s\n", payload.c_str());
        USBTelemetryPort.println(payload.c_str());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Check for serial input often
  }
}

// --- MQTT & Telemetry ---

std::string PrepareTelemetryPayload(const std::string &event) {

  JsonDocument payload;
  payload["client_id"] = espChipIdStr;
  payload["event"] = event;
  payload["units"] = systemSettings.units;

  JsonObject data = payload["data"].to<JsonObject>();

  for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
    double t = a_currentTemperatures[i];

    if (systemSettings.units == "F") {
      if (t > -90.0)
        t = (t * 1.8) + 32;
    }

    String tkey = "TEMP_" + String(i + 1);
    data["temps"][tkey] = (t > -90.0) ? String(t, 1).toFloat() : 0.0f;
  }

  if (ACTIVE_THERMISTORS > 1) {
    int k = 0;
    for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
      for (int j = i + 1; j < ACTIVE_THERMISTORS; ++j) {
        double t = a_currentTemperatures[ACTIVE_THERMISTORS + k];

        if (systemSettings.units == "F") {
          if (t > -90.0)
            t = (t * 1.8) + 32;
        }

        String tkey = "DELTA_T" + String(a_ThermistorIds[i] + 1) + "_T" +
                      String(a_ThermistorIds[j] + 1);
        data["temps"][tkey] = (t > -90.0) ? String(t, 1).toFloat() : 0.0f;
        k++;
      }
    }
  }

  for (int i = 0; i < ACTIVE_FANS; ++i) {
    std::string fkey = String("FAN_" + a_FanNames[i]).c_str();
    data["fans"][fkey] = a_CurrentFanSpeedsRpm[i];
  }

  String buffer;
  serializeJson(payload, buffer);
  return buffer.c_str();
}

String GenerateSensorsJson() {
  JsonDocument doc;
  String buffer;

  for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
    int f_idx = a_ThermistorIds[i];
    String f_key = "TEMP_" + String(f_idx + 1);
    doc["sensors"][f_idx] = f_key;
  }

  // add deltas for each sensor combination if more than 1 sensor and only
  // unique combinations, e.g. T1-T2, T1-T3, T2-T3
  if (ACTIVE_THERMISTORS > 1) {
    int k = 0;
    for (int i = 0; i < ACTIVE_THERMISTORS; ++i) {
      for (int j = i + 1; j < ACTIVE_THERMISTORS; ++j) {
        String f_key = "DELTA_T" + String(a_ThermistorIds[i] + 1) + "_T" +
                       String(a_ThermistorIds[j] + 1);
        doc["sensors"][ACTIVE_THERMISTORS + k] = f_key;
        k++;
      }
    }
  }

  serializeJson(doc, buffer);
  return buffer;
}

// --- HTTP Server ---

void HandleHttpNotFound(AsyncWebServerRequest *request) {
  if (systemSettings.setup_done || systemSettings.offline_mode) {
    // If not setup/offline, try captive portal redirect
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader("Location", AP_LOCAL_URL);
    request->send(response);
  } else {
    request->send(404, "text/plain", "Not Found");
  }
}

void StreamFile(const String &path, const String &mimetype, bool cache,
                AsyncWebServerRequest *request) {
  if (LittleFS.exists(path)) {
    AsyncWebServerResponse *response =
        request->beginResponse(LittleFS, path, mimetype);
    if (cache) {
      response->addHeader("Cache-Control", "max-age=60, immutable");
    }
    request->send(response);
  } else {
    Serial.printf("HTTP: File %s not found!\n", path.c_str());
    request->send(404, "text/plain", "File Not Found");
  }
}

void InitializeHttpServer() {
  if (systemSettings.offline_mode || !systemSettings.setup_done) {
    dnsServer.start(53, "*", AP_LOCAL_IP);
    // Captive Portal Handlers
    webServer.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
      request->redirect("http://logout.net");
    });
    webServer.on("/wpad.dat",
                 [](AsyncWebServerRequest *request) { request->send(404); });
    webServer.on("/generate_204", [](AsyncWebServerRequest *request) {
      request->redirect(AP_LOCAL_URL);
    });
    webServer.on("/redirect", [](AsyncWebServerRequest *request) {
      request->redirect(AP_LOCAL_URL);
    });
    webServer.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) {
      request->redirect(AP_LOCAL_URL);
    });
    webServer.on("/canonical.html", [](AsyncWebServerRequest *request) {
      request->redirect(AP_LOCAL_URL);
    });
    webServer.on("/success.txt",
                 [](AsyncWebServerRequest *request) { request->send(200); });
    webServer.on("/ncsi.txt", [](AsyncWebServerRequest *request) {
      request->redirect(AP_LOCAL_URL);
    });
    webServer.on("/favicon.ico",
                 [](AsyncWebServerRequest *request) { request->send(404); });
  }

  webServer.onNotFound(HandleHttpNotFound);

  webServer.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\": \"ok\"}");
  });

  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!systemSettings.setup_done) {
      request->redirect("/setup");
    } else {
      StreamFile("/index.html", "text/html", false, request);
    }
  });

  webServer.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (systemSettings.setup_done) {
      request->redirect("/");
    } else {
      StreamFile("/setup.html", "text/html", false, request);
    }
  });

  webServer.on("/fans", HTTP_GET, [](AsyncWebServerRequest *request) {
    StreamFile("/fans.html", "text/html", false, request);
  });
  webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    StreamFile("/settings.html", "text/html", false, request);
  });
  webServer.on("/rgb", HTTP_GET, [](AsyncWebServerRequest *request) {
    StreamFile("/rgb.html", "text/html", false, request);
  });
  webServer.on("/all-jquery-deps.min.js", [](AsyncWebServerRequest *request) {
    StreamFile("/all-jquery-deps.min.js", "text/javascript", true, request);
  });
  webServer.on("/styles.css", [](AsyncWebServerRequest *request) {
    StreamFile("/styles.css", "text/css", true, request);
  });
  webServer.on("/logo.png", [](AsyncWebServerRequest *request) {
    StreamFile("/logo.png", "text/image", true, request);
  });

  // API: Get RGB settings
  webServer.on("/get-rgb", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    for (const auto &[key, value] : m_LedSettings) {
      String fkey = "LED_" + String(key);
      doc[fkey]["mode"] = value.mode;
      doc[fkey]["speed"] = value.speed;
      doc[fkey]["start_color"] = value.start_color;
      doc[fkey]["end_color"] = value.end_color;
      doc[fkey]["num_leds"] = value.num_leds;
    }
    String buffer;
    serializeJson(doc, buffer);
    request->send(200, "application/json", buffer);
  });

  // API: Save RGB settings
  webServer.on("/save-rgb", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter *p = request->getParam(i);
      String led_name = p->name();
      String led_data = p->value();
      int led_index = led_name.substring(4).toInt(); // Assumes "LED_X" format

      if (m_LedSettings.count(led_index)) {
        Serial.printf("Saving %s: %s\n", led_name.c_str(), led_data.c_str());
        systemPreferences.putString(led_name.c_str(), led_data);
        JsonDocument led_doc;
        deserializeJson(led_doc, led_data);
        m_LedSettings[led_index].prev_mode = m_LedSettings[led_index].mode;
        m_LedSettings[led_index].mode = led_doc["mode"];
        m_LedSettings[led_index].speed = led_doc["speed"];
        m_LedSettings[led_index].start_color = led_doc["start_color"];
        m_LedSettings[led_index].end_color = led_doc["end_color"];
        m_LedSettings[led_index].num_leds = led_doc["num_leds"];
      }
    }
    request->send(200, "application/json", "{\"status\": \"led_saved\"}");
  });

  // API: Scan WiFi Networks
  webServer.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    ScanWifiNetworks(doc);

    String buffer;
    serializeJson(doc, buffer);

    request->send(200, "application/json", buffer);
  });

  // API: Get Sensors
  webServer.on("/get-sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    String buffer = GenerateSensorsJson();
    request->send(200, "application/json", buffer);
  });

  // API: Get Settings
  webServer.on("/get-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["ssid"] = systemSettings.ssid;
    doc["password"] =
        systemSettings.password; // Note: Sending password - consider security
    doc["hostname"] = systemSettings.hostname;
    doc["tel_itv"] = systemSettings.telemetry_interval;
    doc["setup_done"] = systemSettings.setup_done;
    doc["offline_mode"] = systemSettings.offline_mode;
    doc["units"] = systemSettings.units;
    doc["mqtt_broker"] = systemSettings.mqtt_broker;
    doc["mqtt_topic"] = systemSettings.mqtt_topic;
    doc["mqtt_enable"] = systemSettings.mqtt_enable;
    doc["mqtt_username"] = systemSettings.mqtt_username;
    doc["mqtt_password"] =
        systemSettings.mqtt_password; // Note: Sending password
    doc["mqtt_port"] = systemSettings.mqtt_port;
    doc["fan_passthrough"] = systemSettings.fan_passthrough;
    doc["screen_rotation"] = systemSettings.screen_rotation;
    String buffer;
    serializeJson(doc, buffer);
    request->send(200, "application/json", buffer);
  });

  // API: Save Settings
  webServer.on("/save-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
    bool needs_reboot =
        request->hasParam("force_reboot", true) &&
        request->getParam("force_reboot", true)->value() == "true";
    bool offline_mode =
        request->hasParam("offline_mode", true) &&
        request->getParam("offline_mode", true)->value() == "true";
    if (systemSettings.offline_mode != offline_mode)
      needs_reboot = true;

    if (request->hasParam("ssid", true))
      systemSettings.ssid = request->getParam("ssid", true)->value();
    if (request->hasParam("password", true))
      systemSettings.password = request->getParam("password", true)->value();
    if (request->hasParam("hostname", true))
      systemSettings.hostname = request->getParam("hostname", true)->value();
    systemSettings.offline_mode = offline_mode;
    systemSettings.setup_done = true;

    if (request->hasParam("tel_itv", true))
      systemSettings.telemetry_interval =
          request->getParam("tel_itv", true)->value().toInt();
    if (request->hasParam("units", true))
      systemSettings.units = request->getParam("units", true)->value();
    systemSettings.mqtt_enable =
        request->hasParam("mqtt_enable", true) &&
        request->getParam("mqtt_enable", true)->value() == "true";
    if (request->hasParam("mqtt_username", true))
      systemSettings.mqtt_username =
          request->getParam("mqtt_username", true)->value();
    if (request->hasParam("mqtt_password", true))
      systemSettings.mqtt_password =
          request->getParam("mqtt_password", true)->value();
    if (request->hasParam("mqtt_topic", true))
      systemSettings.mqtt_topic =
          request->getParam("mqtt_topic", true)->value();
    if (request->hasParam("mqtt_broker", true))
      systemSettings.mqtt_broker =
          request->getParam("mqtt_broker", true)->value();
    if (request->hasParam("mqtt_port", true))
      systemSettings.mqtt_port =
          request->getParam("mqtt_port", true)->value().toInt();
    if (request->hasParam("fan_passthrough", true))
      systemSettings.fan_passthrough =
          request->getParam("fan_passthrough", true)->value().toInt();
    if (request->hasParam("screen_rotation", true))
      systemSettings.screen_rotation =
          request->getParam("screen_rotation", true)->value().toInt();

    SaveConfig(systemSettings);
    request->send(200, "application/json", "{\"status\": \"settings_saved\"}");

    if (needs_reboot) {
      request->onDisconnect([]() {
        Serial.println("Settings saved, rebooting now...");
        delay(1000);
        esp_restart();
      });
      return;
    }

    RunPostSetup();
  });

  // API: Clear Settings
  webServer.on("/clear-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    ClearPreferences();
    request->send(200, "application/json",
                  "{\"status\": \"settings_cleared_restarting\"}");
    request->onDisconnect([]() {
      Serial.println("Settings cleared, rebooting now...");
      delay(1000);
      esp_restart();
    });
  });

  // API: Get Fans & Temperatures
  webServer.on("/get-curves", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    for (const auto &[key, value] : m_SensorSettings) {
      String fkey = "FAN_" + String(key);
      doc[fkey]["sensor"] = value.sensor_id;
      doc[fkey]["temp_th"] = value.temperature_alarm_threshold;
      doc[fkey]["duty_th"] = value.rpm_alarm_threshold;
      doc[fkey]["sud_dur"] = value.step_duration_seconds;
      doc[fkey]["halt_on"] = value.halt_on;
      doc[fkey]["units"] = systemSettings.units;
      doc[fkey]["mode"] = value.mode;
      doc[fkey]["pid_kp"] = value.pid_kp;
      doc[fkey]["pid_ki"] = value.pid_ki;
      doc[fkey]["pid_kd"] = value.pid_kd;
      doc[fkey]["pid_setpoint"] = value.pid_setpoint;
      doc[fkey]["min_duty"] = value.min_duty;
      doc[fkey]["max_duty"] = value.max_duty;
      JsonArray curves = doc[fkey]["curves"].to<JsonArray>();
      for (const auto &setting : value.fan_speed_curve) {
        JsonObject point = curves.add<JsonObject>();
        point["temp"] = setting.temperature_threshold;
        point["fan"] = setting.fan_duty_cycle;
      }
    }
    String buffer;
    serializeJson(doc, buffer);
    request->send(200, "application/json", buffer);
  });

  // API: Backup
  webServer.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(
        200, "application/json", GenerateBackupJsonString());
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"waku-ctl-backup.json\"");
    request->send(response);
  });

  // API: Restore
  webServer.on("/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("config", true)) { // true = POST param
      String config_str = request->getParam("config", true)->value();
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, config_str);
      if (!error) {
        RestoreBackupFromJson(doc);
        request->send(200, "application/json",
                      "{\"status\": \"restored_restarting\"}");
        request->onDisconnect([]() {
          Serial.println("Restored backup, rebooting...");
          delay(1000);
          esp_restart();
        });
      } else {
        request->send(400, "application/json", "{\"error\": \"invalid_json\"}");
      }
    } else {
      request->send(400, "application/json",
                    "{\"error\": \"missing_config_param\"}");
    }
  });

  // API: Save Fans & Temperatures
  webServer.on("/save-curves", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter *p = request->getParam(i);
      String fan_name = p->name();
      String fan_data = p->value();
      int fan_id = fan_name.substring(4).toInt(); // Assumes "FAN_X"

      if (m_SensorSettings.count(fan_id)) {
        Serial.printf("Saving %s: %s\n", fan_name.c_str(), fan_data.c_str());
        systemPreferences.putString(fan_name.c_str(), fan_data);

        JsonDocument fan_doc;
        deserializeJson(fan_doc, fan_data);
        m_SensorSettings[fan_id].sensor_id = fan_doc["sensor"].as<int>();
        m_SensorSettings[fan_id].temperature_alarm_threshold =
            fan_doc["temp_th"].as<int>();
        m_SensorSettings[fan_id].rpm_alarm_threshold =
            fan_doc["duty_th"].as<int>();
        m_SensorSettings[fan_id].step_duration_seconds =
            fan_doc["sud_dur"].as<uint8_t>();
        m_SensorSettings[fan_id].halt_on = fan_doc["halt_on"].as<uint8_t>();
        m_SensorSettings[fan_id].mode = fan_doc["mode"].as<uint8_t>();
        m_SensorSettings[fan_id].pid_kp = fan_doc["pid_kp"].as<double>();
        m_SensorSettings[fan_id].pid_ki = fan_doc["pid_ki"].as<double>();
        m_SensorSettings[fan_id].pid_kd = fan_doc["pid_kd"].as<double>();
        m_SensorSettings[fan_id].pid_setpoint =
            fan_doc["pid_setpoint"].as<double>();
        if (fan_doc["min_duty"].is<uint8_t>()) {
          m_SensorSettings[fan_id].min_duty = fan_doc["min_duty"].as<uint8_t>();
        } else {
          m_SensorSettings[fan_id].min_duty = 51;
        }
        if (fan_doc["max_duty"].is<uint8_t>()) {
          m_SensorSettings[fan_id].max_duty = fan_doc["max_duty"].as<uint8_t>();
        } else {
          m_SensorSettings[fan_id].max_duty = 255;
        }
        m_SensorSettings[fan_id].fan_speed_curve.clear();
        for (const auto &setting : fan_doc["curves"].as<JsonArray>()) {
          m_SensorSettings[fan_id].fan_speed_curve.push_back(
              {setting["temp"].as<float>(), setting["fan"].as<int>()});
        }
      }
    }
    request->send(200, "application/json", "{\"status\": \"curves_saved\"}");

    // Restart PID task to apply new settings
    if (gProcessPIDControllerTaskHandle != NULL) {
      vTaskDelete(gProcessPIDControllerTaskHandle);
      gProcessPIDControllerTaskHandle =
          NULL; // Important to avoid using a stale handle
    }
    xTaskCreate(ProcessPIDControllerTask, "ProcessPIDControllerTask", 4096,
                NULL, 5, &gProcessPIDControllerTaskHandle);
  });

  // API: Get Current Data
  webServer.on("/get-data", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json",
                  PrepareTelemetryPayload("manual_fetch").c_str());
  });

  ElegantOTA.begin(&webServer);
  webServer.begin();
  Serial.println("HTTP server started.");
}

// --- Utilities ---

void ClearPreferences() {
  systemPreferences.clear();
  Serial.println("Preferences cleared.");
}