#include "peripherals_manager.h"

void IRAM_ATTR Fan0TachIsr() { unsigned long m = millis(); if ((m - fan0_TS2) > FAN_DEBOUNCE_MS) { fan0_TS1 = fan0_TS2; fan0_TS2 = m; } }
void IRAM_ATTR Fan1TachIsr() { unsigned long m = millis(); if ((m - fan1_TS2) > FAN_DEBOUNCE_MS) { fan1_TS1 = fan1_TS2; fan1_TS2 = m; } }
void IRAM_ATTR Fan2TachIsr() { unsigned long m = millis(); if ((m - fan2_TS2) > FAN_DEBOUNCE_MS) { fan2_TS1 = fan2_TS2; fan2_TS2 = m; } }
void IRAM_ATTR Fan3TachIsr() { unsigned long m = millis(); if ((m - fan3_TS2) > FAN_DEBOUNCE_MS) { fan3_TS1 = fan3_TS2; fan3_TS2 = m; } }

void InitializeAdc() {
    ads.setGain(GAIN_TWOTHIRDS);
    ads.begin();
    Serial.println("ADS1115 configured.");
}

double ReadTemperature(int channel) {
    int16_t adc_raw = ads.readADC_SingleEnded(channel);
    if (adc_raw < 0) {
        Serial.printf("ADS read error on channel %d: %d\n", channel, adc_raw);
        return -1; // Error reading ADC
    } 
    // With GAIN_TWOTHIRDS, the full-scale range is +/- 6.144V
    // This corresponds to a resolution of 0.1875mV per bit.
    // But it's better to calculate voltage based on the max value.
    double voltage = (adc_raw * 6.144) / 32767.0;
    
    // The thermistor is in a voltage divider with a 10k resistor (T_REFERENCE_RESISTANCE)
    // connected to 3.3V (ADC_VOLTAGE). The voltage is measured across the thermistor.
    // V_out = V_in * R_thermistor / (R_ref + R_thermistor)
    // Solving for R_thermistor:
    // R_thermistor = R_ref * V_out / (V_in - V_out)
    double resistance = T_REFERENCE_RESISTANCE * (voltage / (ADC_VOLTAGE - voltage));

    const double inverse_kelvin = 1.0 / (T_NOMINAL_TEMPERATURE + 273.15) +
                        log(resistance / T_NOMINAL_RESISTANCE) / T_B_VALUE;

    double kelvin = 1.0 / inverse_kelvin;

    double celsius = kelvin - 273.15;
    
    printf("Temperature on channel %d: %.2f C (Resistance: %.2f Ohm)\n", channel, celsius, resistance);

    return celsius;
}

void InitializeScreen() {
    if (!oledDisplay.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true); // Halt
    }
    oledDisplay.clearDisplay();
    oledDisplay.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    oledDisplay.setTextSize(1);
    oledDisplay.println("WaKu-ctl Starting...");
    oledDisplay.display();
    delay(1000);
    Serial.println("Display configured.");
}

void InitializeOutputs() {
    pinMode(PIN_LED_HEADER_1, OUTPUT);
    pinMode(PIN_LED_HEADER_2, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    pinMode(PIN_LED_EXT_CTRL_1, OUTPUT);
    digitalWrite(PIN_LED_EXT_CTRL_1, LOW);
    pinMode(PIN_LED_EXT_CTRL_2, OUTPUT);
    digitalWrite(PIN_LED_EXT_CTRL_2, LOW);

#ifdef THREE_STATE_BUFFER_VERSION
    pinMode(PIN_LED_TSB_CTRL_1, OUTPUT);
    digitalWrite(PIN_LED_TSB_CTRL_1, HIGH);
    pinMode(PIN_LED_TSB_CTRL_2, OUTPUT);
    digitalWrite(PIN_LED_TSB_CTRL_2, HIGH);
#endif

    Serial.println("Outputs configured.");
}

void InitializeInputs() {
    pinMode(PIN_RESET_SETTINGS, INPUT_PULLUP);
    analogReadResolution(12); // Corresponds to ESP32_ANALOG_RESOLUTION (4095)
    
    void (*isr_functions[ACTIVE_FANS])() = {Fan0TachIsr, Fan1TachIsr, Fan2TachIsr, Fan3TachIsr};

    for (int i = 0; i < ACTIVE_FANS; i++) {
        int fan_id = a_FanIds[i];
        uint8_t tach_pin = PIN_FAN_MAP[fan_id].tach_pin;
        uint8_t pwm_pin = PIN_FAN_MAP[fan_id].pwm_pin;

        pinMode(tach_pin, INPUT_PULLDOWN);
        Serial.printf("Setting pull-down on TACH %d (Pin %d)\n", fan_id, tach_pin);

        attachInterrupt(digitalPinToInterrupt(tach_pin), isr_functions[i], RISING);
        Serial.printf("Attached ISR to TACH %d (Pin %d)\n", fan_id, tach_pin);

        ledcAttach(pwm_pin, PWM_SIGNAL_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
        Serial.printf("Attaching PWM channel to FAN %d (Pin %d)\n", fan_id, pwm_pin);

        // Start fans at 25% until curves are loaded
        ledcWrite(pwm_pin, MapFanPercentToPwm(25));
    }

    Serial.println("Inputs configured.");
}

void InitializeLeds() {
    uint8_t led_pins[ACTIVE_LED_STRIPS] = {PIN_LED_HEADER_1, PIN_LED_HEADER_2};

    for (int i = 0; i < ACTIVE_LED_STRIPS; i++) {
        String led_prefs_key = "LED_" + String(i);
        String led_prefs = systemPreferences.getString(led_prefs_key.c_str(), "{}");
        JsonDocument led_doc;

        Serial.printf("Loading %s - %s\n", led_prefs_key.c_str(), led_prefs.c_str());

        DeserializationError error = deserializeJson(led_doc, led_prefs);

        if (error || led_prefs == "{}") {
            Serial.printf("No/Invalid settings for %s, using defaults.\n", led_prefs_key.c_str());
            // Use default constructor values from types.h
            m_LedSettings[i] = LedSettings();

            // Save defaults back
            led_doc["mode"] = m_LedSettings[i].mode;
            led_doc["speed"] = m_LedSettings[i].speed;
            led_doc["start_color"] = m_LedSettings[i].start_color;
            led_doc["end_color"] = m_LedSettings[i].end_color;
            led_doc["num_leds"] = m_LedSettings[i].num_leds;
            String settings_json;
            serializeJson(led_doc, settings_json);
            systemPreferences.putString(led_prefs_key.c_str(), settings_json);
        } else {
            m_LedSettings[i].prev_mode = -1; // Initialize to -1 to force initial mode change
            m_LedSettings[i].mode = led_doc["mode"].as<uint8_t>();
            m_LedSettings[i].speed = led_doc["speed"].as<uint8_t>();
            m_LedSettings[i].start_color = led_doc["start_color"].as<uint32_t>();
            m_LedSettings[i].end_color = led_doc["end_color"].as<uint32_t>();
            m_LedSettings[i].num_leds = led_doc["num_leds"].as<uint8_t>();
        }

        Serial.printf("Adding LED %d: %d LEDs, Mode %d\n", i, m_LedSettings[i].num_leds, m_LedSettings[i].mode);
        
        if (i == 0) FastLED.addLeds<WS2812B, PIN_LED_HEADER_1, GRB>(a_LedBuffers[i], MAX_LEDS_PER_STRIP);
        if (i == 1) FastLED.addLeds<WS2812B, PIN_LED_HEADER_2, GRB>(a_LedBuffers[i], MAX_LEDS_PER_STRIP);
    }
}

int MapFanPercentToPwm(int percentage) {
    return MapValue(percentage, 0, 100, 0, 255);
}

int MapValue(int value, int from_low, int from_high, int to_low, int to_high) {
    return (value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low;
}

unsigned long ReadFanRpm(int fan_index) {
    volatile unsigned long *ts1 = nullptr, *ts2 = nullptr;

    switch (fan_index) {
        case 0: ts1 = &fan0_TS1; ts2 = &fan0_TS2; break;
        case 1: ts1 = &fan1_TS1; ts2 = &fan1_TS2; break;
        case 2: ts1 = &fan2_TS1; ts2 = &fan2_TS2; break;
        case 3: ts1 = &fan3_TS1; ts2 = &fan3_TS2; break;
        default: return 0;
    }

    // Disable interrupts briefly to read volatile variables atomically
    unsigned long current_millis = millis();
    unsigned long t1_val, t2_val;
    noInterrupts();
    t1_val = *ts1;
    t2_val = *ts2;
    interrupts();

    if ((current_millis - t2_val) < FAN_STUCK_THRESHOLD_MD && t2_val > t1_val) {
        double delta_t = t2_val - t1_val;
        double rpm = (60000.0 / delta_t) / 2.0; // /2 because 2 pulses per revolution
        return (rpm > 0) ? static_cast<unsigned long>(rpm) : 0;
    }
    return 0; // Stuck or no reading
}

int CalculateFanSpeed(int fan_id, float temperature) {
    auto it = m_SensorSettings.find(fan_id);
    if (it == m_SensorSettings.end()) {
        return 0; // Sensor not found
    }

    const auto& curve = it->second.fan_speed_curve;
    if (curve.empty()) {
        return 0; // No curve defined
    }

    // Find the first threshold the temperature is below or equal to
    for (const auto& point : curve) {
        if (temperature <= point.temperature_threshold) {
            return point.fan_duty_cycle;
        }
    }

    // If temp is higher than all thresholds, use the last (highest) speed
    return curve.back().fan_duty_cycle;
}
