#include "globals.h"
#include <esp_wifi.h> // Used for mpdu_rx_disable android workaround
#include <TaskScheduler.h>
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "peripherals_manager.h"
#include "led_manager.h"

Task *gSendTelemetryTask = nullptr;
Scheduler taskScheduler;

// --- Function Prototypes ---

// Initialization
void InitializeConfig();
void ClearPreferences();
void SaveConfig(const Settings& s);

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
void ReadTemperaturesTask(void *pvParameters);
void PlayLedsTask(void *pvParameters);
void DisplayDataTask(void *pvParameters);
void NativeUsbTelemetryTask(void *pvParameters);
void PlayAlarmsTask(void *pvParameters);

// Telemetry
std::string PrepareTelemetryPayload(const std::string& event = "default");
void SendUsbTelemetry();

// HTTP Server
void HandleHttpNotFound(AsyncWebServerRequest *request);
void StreamFile(const String& path, const String& mimetype, bool cache, AsyncWebServerRequest *request);

// --- Initialization Functions ---

void InitializeConfig() {
    systemPreferences.begin("config", false);
    if (CLEAR_PREFERENCES_ON_EVERY_BOOT) {
        ClearPreferences();
    }

    uint64_t mac = ESP.getEfuseMac();
    char mac_str[18];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
            (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
            (uint8_t)(mac >> 8), (uint8_t)(mac));

    espChipIdStr = mac_str;

    systemSettings.ssid = systemPreferences.getString("ssid", "");
    systemSettings.password = systemPreferences.getString("password", "");
    systemSettings.hostname = systemPreferences.getString("hostname", "waku-ctl.local");
    systemSettings.telemetry_interval = systemPreferences.getInt("tel_itv", TELEMETRY_INTERVAL_MS);
    systemSettings.setup_done = systemPreferences.getBool("setup_done", false);
    systemSettings.offline_mode = systemPreferences.getBool("offline_mode", true);
    systemSettings.units = systemPreferences.getString("units", "C");

    systemSettings.mqtt_broker = systemPreferences.getString("mqtt_broker", "broker.emqx.io");
    systemSettings.mqtt_enable = systemPreferences.getBool("mqtt_enable", false);
    systemSettings.mqtt_topic = systemPreferences.getString("mqtt_topic", "waku-ctl/telemetry/" + espChipIdStr);
    systemSettings.mqtt_username = systemPreferences.getString("mqtt_username", "");
    systemSettings.mqtt_password = systemPreferences.getString("mqtt_password", "");
    systemSettings.mqtt_port = systemPreferences.getInt("mqtt_port", MQTT_DEFAULT_PORT);

    Serial.println("Settings loaded.");
}

void SaveConfig(const Settings& s) {
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
    Serial.println("Settings saved.");
}


void InitializeTasks() {
    xTaskCreate(MonitorStatesTask, "MonitorStates", 4096, NULL, 6, NULL);
    xTaskCreate(ReadTemperaturesTask, "ReadTemps", 6144, NULL, 5, NULL);
    xTaskCreate(PlayLedsTask, "PlayLEDs", 4096, NULL, 4, NULL);
    xTaskCreate(DisplayDataTask, "DisplayData", 4096, NULL, 3, NULL);
    xTaskCreate(NativeUsbTelemetryTask, "UsbTelTask", 2048, NULL, 2, NULL);
    xTaskCreate(PlayAlarmsTask, "PlayAlarms", 2048, NULL, tskIDLE_PRIORITY, NULL);

    InitializeMqttTelemetryTask(taskScheduler, gSendTelemetryTask);
    Serial.println("Tasks initialized.");
}


void InitializeFanCurves() {
    for (int i = 0; i < ACTIVE_FANS; i++) {
        int fan_id = a_FanIds[i];
        String fan_key = "FAN_" + String(fan_id);
        String fan_curves = systemPreferences.getString(fan_key.c_str(), "{}");
        JsonDocument fan_doc;

        Serial.printf("Loading %s - %s\n", fan_key.c_str(), fan_curves.c_str());

        DeserializationError error = deserializeJson(fan_doc, fan_curves);

        if (error || fan_curves == "{}") {
            Serial.printf("No/Invalid settings for %s, using defaults.\n", fan_key.c_str());
            m_SensorSettings[fan_id].sensor_name = "TEMP_1";
            m_SensorSettings[fan_id].temperature_alarm_threshold = 999;
            m_SensorSettings[fan_id].rpm_alarm_threshold = -1;
            m_SensorSettings[fan_id].step_duration_seconds = 1;
            m_SensorSettings[fan_id].fan_speed_curve.push_back({30.0f, MapFanPercentToPwm(30)});
            m_SensorSettings[fan_id].fan_speed_curve.push_back({33.0f, MapFanPercentToPwm(40)});
            m_SensorSettings[fan_id].fan_speed_curve.push_back({36.0f, MapFanPercentToPwm(55)});
            m_SensorSettings[fan_id].fan_speed_curve.push_back({39.0f, MapFanPercentToPwm(75)});
            m_SensorSettings[fan_id].fan_speed_curve.push_back({41.0f, MapFanPercentToPwm(100)});

            fan_doc["sensor"] = m_SensorSettings[fan_id].sensor_name;
            fan_doc["curves"] = fan_doc.to<JsonArray>();
            for (const auto& setting : m_SensorSettings[fan_id].fan_speed_curve) {
                JsonObject curve_point = fan_doc["curves"].add<JsonObject>();
                curve_point["temp"] = setting.temperature_threshold;
                curve_point["fan"] = setting.fan_duty_cycle;
            }
            fan_doc["temp_th"] = m_SensorSettings[fan_id].temperature_alarm_threshold;
            fan_doc["duty_th"] = m_SensorSettings[fan_id].rpm_alarm_threshold;
            fan_doc["sud_dur"] = m_SensorSettings[fan_id].step_duration_seconds;

            String settings_json;
            serializeJson(fan_doc, settings_json);
            systemPreferences.putString(fan_key.c_str(), settings_json);
        } else {
            m_SensorSettings[fan_id].sensor_name = fan_doc["sensor"].as<String>();
            m_SensorSettings[fan_id].temperature_alarm_threshold = fan_doc["temp_th"].as<int>();
            m_SensorSettings[fan_id].rpm_alarm_threshold = fan_doc["duty_th"].as<int>();
            m_SensorSettings[fan_id].step_duration_seconds = fan_doc["sud_dur"].as<uint8_t>();
            m_SensorSettings[fan_id].fan_speed_curve.clear();
            for (auto const& setting : fan_doc["curves"].as<JsonArray>()) {
                m_SensorSettings[fan_id].fan_speed_curve.push_back({setting["temp"].as<float>(), setting["fan"].as<int>()});
            }
        }
    }

    // Set initial fan speeds based on current temps
    const double t1 = ReadTemperature(0);
    const double t2 = ReadTemperature(1);

    for (int i = 0; i < ACTIVE_FANS; i++) {
        int fan_id = a_FanIds[i];
        const double temp = (m_SensorSettings[fan_id].sensor_name == "TEMP_1") ? t1 : t2;
        const int target_speed = (temp > 0) ? CalculateFanSpeed(fan_id, temp) : MapFanPercentToPwm(25);

        m_TargetFanRpm[fan_id].current_rpm = target_speed;
        m_TargetFanRpm[fan_id].target_rpm = target_speed;
        ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target_speed);
    }
}


// --- Core Logic & Tasks ---

void setup() {
    USB.PID(0x82E5);
    USB.VID(0x303A);
    USB.productName("WaKu Controller");
    USB.manufacturerName("kenny's Labs");
    USBTelemetryPort.begin();
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

    InitializeConfig();
    InitializeOutputs();
    InitializeInputs();
    InitializeScreen();
    InitializeWifi();
    InitializeHttpServer();

    if (!systemSettings.setup_done) {
        Serial.println("Setup not complete. Waiting for configuration via AP/Web Server.");

        oledDisplay.clearDisplay();
        oledDisplay.setCursor(0, 0);
        oledDisplay.printf("   ### SETUP ###\n\n");
        oledDisplay.printf("SSID: WaKu-ctl\nIP: %s", AP_LOCAL_IP.toString().c_str());
        oledDisplay.display();
        
        return; // Wait for user to complete setup via web
    }

    RunPostSetup();
    Serial.println("--- WaKu-ctl Ready ---");
}

void RunPostSetup() {
    Serial.println("Running Post-Setup...");
    InitializeMqttClient();
    //InitializeNtpTime(); // Optional, not used for now
    InitializeAdc();
    InitializeFanCurves();
    InitializeLeds();
    InitializeTasks();
    b_BootCompleted = true;
    Serial.println("Post-Setup Complete.");
}

void loop() {
    if(b_BootCompleted) {
        taskScheduler.execute();
        LoopMqttClient();
    }
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
                 Serial.println("Holding > 5s. Clearing preferences & rebooting!");
                 ClearPreferences();
                 delay(1000);
                 esp_restart();
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
        vTaskDelay(pdMS_TO_TICKS(250)); // Check ~4 times a second
    }     
}   

void MonitorStatesTask(void *pvParameters) {
    while (true) {
        // --- Monitor Alarms ---
        bool temp_alarm_active = false;
        bool rpm_alarm_active = false;
        const double t1 = ReadTemperature(0);
        const double t2 = ReadTemperature(1);

        for (int i = 0; i < ACTIVE_FANS; ++i) {
            int fan_id = a_FanIds[i];
            const auto& settings = m_SensorSettings[fan_id];
            const double temp = (settings.sensor_name == "TEMP_1") ? t1 : t2;
            const unsigned long current_rpm = a_CurrentFanSpeedsRpm[i];

            // Temperature Alarm
            if (temp > 0 && settings.temperature_alarm_threshold > 0 && temp >= settings.temperature_alarm_threshold) {
                temp_alarm_active = true;
                if (!b_TempAlarmFiring) Serial.printf("ALARM: Temp high on %s (%.1fC)\n", settings.sensor_name.c_str(), temp);
            }

            // RPM Alarm (only if threshold is set, > 0)
            if (settings.rpm_alarm_threshold >= 0 && current_rpm < (unsigned long)settings.rpm_alarm_threshold) {
                rpm_alarm_active = true;
                if (!b_RpmAlarmFiring) Serial.printf("ALARM: RPM low on FAN_%d (%lu RPM)\n", fan_id, current_rpm);
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
        } else {
            noTone(PIN_BUZZER);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }    
}

void ReadTemperaturesTask(void *pvParameters) {
    while (true) {

        const double t1 = ReadTemperature(0);
        const double t2 = ReadTemperature(1);

        if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
            Serial.printf("T1: %.2f C; T2: %.2f C\n", t1, t2);
        }

        for (int i = 0; i < ACTIVE_FANS; ++i) {
            int fan_id = a_FanIds[i];
            a_CurrentFanSpeedsRpm[i] = ReadFanRpm(i); // Use index 'i' for ReadFanRpm

            const auto& settings = m_SensorSettings[fan_id];
            const double temp = (settings.sensor_name == "TEMP_1") ? t1 : t2;

            if (temp <= 0) {
                if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) Serial.printf("Temp sensor N/A for FAN_%d. Skipping.\n", fan_id);
                continue; // Skip if temp sensor not working/connected
            }

            auto& target = m_TargetFanRpm[fan_id];
            int new_target_pwm = CalculateFanSpeed(fan_id, temp);

            if (new_target_pwm != target.target_rpm && !target.is_adjusting) {
                target.target_rpm = new_target_pwm;
                target.step_value = (target.target_rpm - target.current_rpm) / settings.step_duration_seconds;
                if (target.step_value == 0 && target.target_rpm != target.current_rpm) {
                    target.step_value = (target.target_rpm > target.current_rpm) ? 1 : -1;
                }

                if (target.step_value != 0) {
                    target.start_time_ms = millis();
                    target.is_adjusting = true;
                    Serial.printf("FAN_%d: Adjusting %d -> %d (Step: %d)\n", fan_id, target.current_rpm, target.target_rpm, target.step_value);
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
                } else if (target.step_value < 0 && target.current_rpm <= target.target_rpm) {
                    target.current_rpm = target.target_rpm;
                    reached = true;
                }

                if (reached) {
                    Serial.printf("FAN_%d: Reached target %d\n", fan_id, target.target_rpm);
                    target.is_adjusting = false;
                }
                ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target.current_rpm);

            } else if (!target.is_adjusting) {
                // Ensure it stays at target if not adjusting
                ledcWrite(PIN_FAN_MAP[fan_id].pwm_pin, target.target_rpm);
            }

            if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
                Serial.printf("FAN_%d RPM: %lu (Target PWM: %d, Current PWM: %d)\n",
                              fan_id, a_CurrentFanSpeedsRpm[i], target.target_rpm, target.current_rpm);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250)); // Read temps/adjust fans twice a second
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

void DisplayDataTask(void *pvParameters) {
    while (true) {
        oledDisplay.clearDisplay();
        oledDisplay.setCursor(0, 0);

        switch (currentScreen) {
            case ScreenView::Overview:
                oledDisplay.printf("   ### OVERVIEW ###\n\n");
                oledDisplay.printf("Mode: %s\n", systemSettings.offline_mode ? "Offline" : "Connected");
                oledDisplay.printf("IP: %s\n", systemSettings.offline_mode ? AP_LOCAL_IP.toString().c_str() : WiFi.localIP().toString().c_str());
                oledDisplay.printf("Units: %s\n", systemSettings.units.c_str());
                oledDisplay.printf("Temp Alarm: %s\n", b_TempAlarmFiring ? "Yes" : "No");
                oledDisplay.printf("RPM Alarm: %s\n", b_RpmAlarmFiring ? "Yes" : "No");
                oledDisplay.setCursor(50, 56);
                oledDisplay.printf("o...");
                break;

            case ScreenView::Temperatures:
                {
                    double t1 = ReadTemperature(0);
                    double t2 = ReadTemperature(1);

                    if (systemSettings.units == "F") {
                        if (t1 > -90.0) t1 = (t1 * 1.8) + 32;
                        if (t2 > -90.0) t2 = (t2 * 1.8) + 32;
                    }

                    oledDisplay.printf(" ### TEMPERATURE ###\n\n");
                    oledDisplay.printf("TEMP1: %s\n", ((t1 < 0 && systemSettings.units == "C") || t1 < 32 && systemSettings.units == "F") ? "N/A" : (String(t1, 1) + systemSettings.units).c_str());
                    oledDisplay.printf("TEMP2: %s\n", ((t2 < 0 && systemSettings.units == "C") || t2 < 32 && systemSettings.units == "F") ? "N/A" : (String(t2, 1) + systemSettings.units).c_str());
                    oledDisplay.setCursor(50, 56);
                    oledDisplay.printf(".o..");
                }
                break;

            case ScreenView::Fans:
                oledDisplay.printf("  ### FAN SPEED ### \n\n");
                for (int i = 0; i < ACTIVE_FANS; i++) {
                    oledDisplay.printf("FAN %d: %4lu RPM\n", a_FanIds[i], a_CurrentFanSpeedsRpm[i]);
                }
                oledDisplay.setCursor(50, 56);
                oledDisplay.printf("..o.");
                break;

            case ScreenView::Rgb:
                oledDisplay.printf("  ### RGB MODE ### \n\n");
                for (const auto& [key, value] : m_LedSettings) {
                    std::string fkey = "LED_" + std::to_string(key);
                    std::string led_mode = "Unknown";
                    switch (value.mode) {
                        case 0: led_mode = "Off"; break;
                        case 1: led_mode = "Static"; break;
                        case 2: led_mode = "Grad/Wave"; break;
                        case 3: led_mode = "Grad/Move"; break;
                        case 4: led_mode = "Rainbow"; break;
                        case 5: led_mode = "Passthrough"; break;
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

void NativeUsbTelemetryTask(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(1000); // Send every 1 second

    while (true) {
        vTaskDelayUntil(&last_wake_time, frequency);
        SendUsbTelemetry();
    }
}

// --- MQTT & Telemetry ---

void SendUsbTelemetry() {
    if (USBTelemetryPort) {
        std::string payload = PrepareTelemetryPayload("usb_stream");
        size_t sent_bytes = USBTelemetryPort.println(payload.c_str());
        if (DEBUG_ENABLED && DEBUG_DATA_ENABLED) {
            Serial.printf("USB: Sent %d bytes.\n", sent_bytes);
        }
    }
}

std::string PrepareTelemetryPayload(const std::string& event) {
    double t1 = ReadTemperature(0);
    double t2 = ReadTemperature(1);

    if (systemSettings.units == "F") {
        if (t1 > -90.0) t1 = (t1 * 1.8) + 32;
        if (t2 > -90.0) t2 = (t2 * 1.8) + 32;
    }

    JsonDocument payload;
    payload["client_id"] = espChipIdStr;
    payload["event"] = event;
    payload["units"] = systemSettings.units;
    JsonObject data = payload["data"].to<JsonObject>();
    
    data["temperature1"] = (t1 > -90.0) ? String(t1, 1).toFloat() : 0.0f;
    data["temperature2"] = (t2 > -90.0) ? String(t2, 1).toFloat() : 0.0f;

    for (int i = 0; i < ACTIVE_FANS; ++i) {
        std::string fkey = "FAN_" + std::to_string(a_FanIds[i]);
        data[fkey] = a_CurrentFanSpeedsRpm[i];
    }

    String buffer;
    serializeJson(payload, buffer);
    return buffer.c_str();
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

void StreamFile(const String& path, const String& mimetype, bool cache, AsyncWebServerRequest *request) {
    if (LittleFS.exists(path)) {
        AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, mimetype);
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
        webServer.on("/connecttest.txt", [](AsyncWebServerRequest *request) { request->redirect("http://logout.net"); });
        webServer.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });
        webServer.on("/generate_204", [](AsyncWebServerRequest *request) { request->redirect(AP_LOCAL_URL); });
        webServer.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(AP_LOCAL_URL); });
        webServer.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(AP_LOCAL_URL); });
        webServer.on("/canonical.html", [](AsyncWebServerRequest *request) { request->redirect(AP_LOCAL_URL); });
        webServer.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); });
        webServer.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(AP_LOCAL_URL); });
        webServer.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(404); });
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

    webServer.on("/fans", HTTP_GET, [](AsyncWebServerRequest *request) { StreamFile("/fans.html", "text/html", false, request); });
    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) { StreamFile("/settings.html", "text/html", false, request); });
    webServer.on("/rgb", HTTP_GET, [](AsyncWebServerRequest *request) { StreamFile("/rgb.html", "text/html", false, request); });
    webServer.on("/all-jquery-deps.min.js", [](AsyncWebServerRequest *request) { StreamFile("/all-jquery-deps.min.js", "text/javascript", true, request); });
    webServer.on("/logo.png", [](AsyncWebServerRequest *request) { StreamFile("/logo.png", "text/image", true, request); });


    // API: Get RGB settings
    webServer.on("/get-rgb", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        for (const auto& [key, value] : m_LedSettings) {
            String fkey = "LED_" + String(key);
            doc[fkey]["mode"] = value.mode;
            doc[fkey]["speed"] = value.speed;
            doc[fkey]["start_color"] = value.start_color;
            doc[fkey]["end_color"] = value.end_color;
            doc[fkey]["num_leds"] = value.num_leds;
        }
        String buffer;
        serializeJsonPretty(doc, buffer);
        request->send(200, "application/json", buffer);
    });

    // API: Save RGB settings
    webServer.on("/save-rgb", HTTP_POST, [](AsyncWebServerRequest *request) {
        int params = request->params();
        for(int i=0; i < params; i++){
            const AsyncWebParameter* p = request->getParam(i);
            String led_name = p->name();
            String led_data = p->value();
            int led_index = led_name.substring(4).toInt(); // Assumes "LED_X" format

            if (m_LedSettings.count(led_index)) {
                 Serial.printf("Saving %s: %s\n", led_name.c_str(), led_data.c_str());
                 systemPreferences.putString(led_name.c_str(), led_data);
                 JsonDocument led_doc;
                 deserializeJson(led_doc, led_data);
                 m_LedSettings[led_index].prev_mode =  m_LedSettings[led_index].mode;
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

    // API: Get Settings
    webServer.on("/get-settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["ssid"] = systemSettings.ssid;
        doc["password"] = systemSettings.password; // Note: Sending password - consider security
        doc["hostname"] = systemSettings.hostname;
        doc["tel_itv"] = systemSettings.telemetry_interval;
        doc["setup_done"] = systemSettings.setup_done;
        doc["offline_mode"] = systemSettings.offline_mode;
        doc["units"] = systemSettings.units;
        doc["mqtt_broker"] = systemSettings.mqtt_broker;
        doc["mqtt_topic"] = systemSettings.mqtt_topic;
        doc["mqtt_enable"] = systemSettings.mqtt_enable;
        doc["mqtt_username"] = systemSettings.mqtt_username;
        doc["mqtt_password"] = systemSettings.mqtt_password; // Note: Sending password
        doc["mqtt_port"] = systemSettings.mqtt_port;
        String buffer;
        serializeJson(doc, buffer);
        request->send(200, "application/json", buffer);
    });

    // API: Save Settings
    webServer.on("/save-settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool needs_reboot = request->hasParam("force_reboot", true) && request->getParam("force_reboot", true)->value() == "true";
        bool offline_mode = request->hasParam("offline_mode", true) && request->getParam("offline_mode", true)->value() == "true";
        if (systemSettings.offline_mode != offline_mode) needs_reboot = true;

        if (request->hasParam("ssid", true)) systemSettings.ssid = request->getParam("ssid", true)->value();
        if (request->hasParam("password", true)) systemSettings.password = request->getParam("password", true)->value();
        if (request->hasParam("hostname", true)) systemSettings.hostname = request->getParam("hostname", true)->value();
        systemSettings.offline_mode = offline_mode;
        systemSettings.setup_done = true;

        if (request->hasParam("tel_itv", true)) systemSettings.telemetry_interval = request->getParam("tel_itv", true)->value().toInt();
        if (request->hasParam("units", true)) systemSettings.units = request->getParam("units", true)->value();
        systemSettings.mqtt_enable = request->hasParam("mqtt_enable", true) && request->getParam("mqtt_enable", true)->value() == "true";
        if (request->hasParam("mqtt_username", true)) systemSettings.mqtt_username = request->getParam("mqtt_username", true)->value();
        if (request->hasParam("mqtt_password", true)) systemSettings.mqtt_password = request->getParam("mqtt_password", true)->value();
        if (request->hasParam("mqtt_topic", true)) systemSettings.mqtt_topic = request->getParam("mqtt_topic", true)->value();
        if (request->hasParam("mqtt_broker", true)) systemSettings.mqtt_broker = request->getParam("mqtt_broker", true)->value();
        if (request->hasParam("mqtt_port", true)) systemSettings.mqtt_port = request->getParam("mqtt_port", true)->value().toInt();

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
        request->send(200, "application/json", "{\"status\": \"settings_cleared_restarting\"}");
        request->onDisconnect([]() {
            Serial.println("Settings cleared, rebooting now...");
            delay(1000);
            esp_restart();
        });
    });

    // API: Get Fan Curves
    webServer.on("/get-curves", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        for (const auto& [key, value] : m_SensorSettings) {
            String fkey = "FAN_" + String(key);
            doc[fkey]["sensor"] = value.sensor_name;
            doc[fkey]["temp_th"] = value.temperature_alarm_threshold;
            doc[fkey]["duty_th"] = value.rpm_alarm_threshold;
            doc[fkey]["sud_dur"] = value.step_duration_seconds;
            doc[fkey]["units"] = systemSettings.units;
            JsonArray curves = doc[fkey]["curves"].to<JsonArray>();
            for (const auto& setting : value.fan_speed_curve) {
                JsonObject point = curves.add<JsonObject>();
                point["temp"] = setting.temperature_threshold;
                point["fan"] = setting.fan_duty_cycle;
            }
        }
        String buffer;
        serializeJsonPretty(doc, buffer);
        request->send(200, "application/json", buffer);
    });

    // API: Save Fan Curves
    webServer.on("/save-curves", HTTP_POST, [](AsyncWebServerRequest *request) {
        int params = request->params();
        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            String fan_name = p->name();
            String fan_data = p->value();
            int fan_id = fan_name.substring(4).toInt(); // Assumes "FAN_X"

            if (m_SensorSettings.count(fan_id)) {
                Serial.printf("Saving %s: %s\n", fan_name.c_str(), fan_data.c_str());
                systemPreferences.putString(fan_name.c_str(), fan_data);

                JsonDocument fan_doc;
                deserializeJson(fan_doc, fan_data);
                m_SensorSettings[fan_id].sensor_name = fan_doc["sensor"].as<String>();
                m_SensorSettings[fan_id].temperature_alarm_threshold = fan_doc["temp_th"].as<int>();
                m_SensorSettings[fan_id].rpm_alarm_threshold = fan_doc["duty_th"].as<int>();
                m_SensorSettings[fan_id].step_duration_seconds = fan_doc["sud_dur"].as<uint8_t>();
                m_SensorSettings[fan_id].fan_speed_curve.clear();
                for (const auto& setting : fan_doc["curves"].as<JsonArray>()) {
                    m_SensorSettings[fan_id].fan_speed_curve.push_back({setting["temp"].as<float>(), setting["fan"].as<int>()});
                }
            }
        }
        request->send(200, "application/json", "{\"status\": \"curves_saved\"}");
    });


    // API: Get Current Data
    webServer.on("/get-data", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", PrepareTelemetryPayload("manual_fetch").c_str());
    });

    webServer.begin();
    Serial.println("HTTP server started.");
}

// --- Utilities ---

void ClearPreferences() {
    systemPreferences.clear();
    Serial.println("Preferences cleared.");
}