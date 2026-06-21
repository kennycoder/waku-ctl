#include "peripherals_manager.h"

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR Fan0TachIsr() {
  portENTER_CRITICAL_ISR(&timerMux);
  unsigned long m = micros();
  if ((m - fan0_last_pulse_micros) > FAN_DEBOUNCE_MICROS) {
    fan0_pulses++;
    fan0_last_pulse_micros = m;
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

void IRAM_ATTR Fan1TachIsr() {
  portENTER_CRITICAL_ISR(&timerMux);
  unsigned long m = micros();
  if ((m - fan1_last_pulse_micros) > FAN_DEBOUNCE_MICROS) {
    fan1_pulses++;
    fan1_last_pulse_micros = m;
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

void IRAM_ATTR Fan2TachIsr() {
  portENTER_CRITICAL_ISR(&timerMux);
  unsigned long m = micros();
  if ((m - fan2_last_pulse_micros) > FAN_DEBOUNCE_MICROS) {
    fan2_pulses++;
    fan2_last_pulse_micros = m;
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

void IRAM_ATTR Fan3TachIsr() {
  portENTER_CRITICAL_ISR(&timerMux);
  unsigned long m = micros();
  if ((m - fan3_last_pulse_micros) > FAN_DEBOUNCE_MICROS) {
    fan3_pulses++;
    fan3_last_pulse_micros = m;
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

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

  // The thermistor is in a voltage divider with a 10k resistor
  // (T_REFERENCE_RESISTANCE) connected to 3.3V (ADC_VOLTAGE). The voltage is
  // measured across the thermistor. V_out = V_in * R_thermistor / (R_ref +
  // R_thermistor) Solving for R_thermistor: R_thermistor = R_ref * V_out /
  // (V_in - V_out)
  double resistance =
      T_REFERENCE_RESISTANCE * (voltage / (ADC_VOLTAGE - voltage));

  const double inverse_kelvin =
      1.0 / (T_NOMINAL_TEMPERATURE + 273.15) +
      log(resistance / T_NOMINAL_RESISTANCE) / T_B_VALUE;

  double kelvin = 1.0 / inverse_kelvin;

  double celsius = kelvin - 273.15;

  // Serial.printf("ADC Channel %d: Raw=%d, Voltage=%.3f V, Resistance=%.2f Ohm,
  // Temperature=%.2f C\n", channel, adc_raw, voltage, resistance, celsius);

  celsius = std::min(std::max(celsius, 0.0), 100.0); // Clamp to 0 to 100 C

  if (isnan(celsius)) {
    celsius = 0.0; // Error value
  }

  return celsius;
}

void InitializeScreen() {
  if (!oledDisplay.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true)
      ; // Halt
  }
  oledDisplay.clearDisplay();
  oledDisplay.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  oledDisplay.setTextSize(1);
  oledDisplay.setRotation(systemSettings.screen_rotation);
  oledDisplay.println("WaKu-ctl Starting...");
  oledDisplay.display();
  delay(1000);
  Serial.println("Display configured.");
}

void InitializeOutputs() {
  pinMode(PIN_LED_HEADER_1, OUTPUT);
  pinMode(PIN_LED_HEADER_2, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, LOW);

  pinMode(PIN_LED_EXT_CTRL_1, OUTPUT);
  digitalWrite(PIN_LED_EXT_CTRL_1, LOW);
  pinMode(PIN_LED_EXT_CTRL_2, OUTPUT);
  digitalWrite(PIN_LED_EXT_CTRL_2, LOW);

  for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
    uint8_t pwm_pin = PIN_FAN_MAP[fan_id].pwm_pin;

    pinMode(pwm_pin, OUTPUT);

    ledcAttach(pwm_pin, PWM_SIGNAL_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    Serial.printf("Attaching PWM channel to FAN %d (Pin %d)\n", fan_id,
                  pwm_pin);

    // Start fans at 25% until curves are loaded
    ledcWrite(pwm_pin, MapFanPercentToPwm(25));
  }

  pinMode(PIN_TACH, OUTPUT);

  Serial.println("Outputs configured.");
}

void InitializeInputs() {
  pinMode(PIN_RESET_SETTINGS, INPUT_PULLUP);
  analogReadResolution(12); // Corresponds to ESP32_ANALOG_RESOLUTION (4095)

  void (*isr_functions[ACTIVE_FANS])() = {Fan0TachIsr, Fan1TachIsr, Fan2TachIsr,
                                          Fan3TachIsr};

  for (int fan_id = 0; fan_id < ACTIVE_FANS; fan_id++) {
    uint8_t tach_pin = PIN_FAN_MAP[fan_id].tach_pin;

    pinMode(tach_pin, INPUT);
    Serial.printf("Setting pull-down on TACH %d (Pin %d)\n", fan_id, tach_pin);

    attachInterrupt(digitalPinToInterrupt(tach_pin), isr_functions[fan_id],
                    RISING);
    Serial.printf("Attached ISR to TACH %d (Pin %d)\n", fan_id, tach_pin);
  }

  Serial.println("Inputs configured.");
}

void InitializeLeds() {
  uint8_t led_pins[ACTIVE_LED_STRIPS] = {PIN_LED_HEADER_1, PIN_LED_HEADER_2};

  for (int i = 0; i < ACTIVE_LED_STRIPS; i++) {
    String led_prefs_key = "LED_" + String(i);
    String led_prefs = systemPreferences.getString(led_prefs_key.c_str(), "{}");
    JsonDocument led_doc;

    Serial.printf("Loading %s - %s\n", led_prefs_key.c_str(),
                  led_prefs.c_str());

    DeserializationError error = deserializeJson(led_doc, led_prefs);

    if (error || led_prefs == "{}") {
      Serial.printf("No/Invalid settings for %s, using defaults.\n",
                    led_prefs_key.c_str());
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
      m_LedSettings[i].prev_mode =
          -1; // Initialize to -1 to force initial mode change
      m_LedSettings[i].mode = led_doc["mode"].as<uint8_t>();
      m_LedSettings[i].speed = led_doc["speed"].as<uint8_t>();
      m_LedSettings[i].start_color = led_doc["start_color"].as<uint32_t>();
      m_LedSettings[i].end_color = led_doc["end_color"].as<uint32_t>();
      m_LedSettings[i].num_leds = led_doc["num_leds"].as<uint8_t>();

      if (m_LedSettings[i].num_leds > MAX_LEDS_PER_STRIP) {
        Serial.printf("Capping LED strip %d length from %d to %d\n", i,
                      m_LedSettings[i].num_leds, MAX_LEDS_PER_STRIP);
        m_LedSettings[i].num_leds = MAX_LEDS_PER_STRIP;
      }
    }

    Serial.printf("Adding LED %d: %d LEDs, Mode %d\n", i,
                  m_LedSettings[i].num_leds, m_LedSettings[i].mode);

    if (i == 0)
      FastLED.addLeds<WS2812B, PIN_LED_HEADER_1, GRB>(a_LedBuffers[i],
                                                      MAX_LEDS_PER_STRIP);
    if (i == 1)
      FastLED.addLeds<WS2812B, PIN_LED_HEADER_2, GRB>(a_LedBuffers[i],
                                                      MAX_LEDS_PER_STRIP);
  }
}

int MapFanPercentToPwm(int percentage) {
  return MapValue(percentage, 0, 100, 0, 255);
}

int MapValue(int value, int from_low, int from_high, int to_low, int to_high) {
  return (value - from_low) * (to_high - to_low) / (from_high - from_low) +
         to_low;
}

unsigned long ReadFanRpm(int fan_index) {
  volatile unsigned long *pulses_ptr = nullptr;
  unsigned long *last_calc_ptr = nullptr;

  switch (fan_index) {
  case 0:
    pulses_ptr = &fan0_pulses;
    last_calc_ptr = &fan0_last_calc_time;
    break;
  case 1:
    pulses_ptr = &fan1_pulses;
    last_calc_ptr = &fan1_last_calc_time;
    break;
  case 2:
    pulses_ptr = &fan2_pulses;
    last_calc_ptr = &fan2_last_calc_time;
    break;
  case 3:
    pulses_ptr = &fan3_pulses;
    last_calc_ptr = &fan3_last_calc_time;
    break;
  default:
    return 0;
  }

  unsigned long current_time = millis();
  unsigned long time_diff = current_time - *last_calc_ptr;

  if (time_diff == 0)
    return 0;

  portENTER_CRITICAL(&timerMux);
  unsigned long count = *pulses_ptr;
  *pulses_ptr = 0;
  portEXIT_CRITICAL(&timerMux);

  *last_calc_ptr = current_time;

  // RPM = (count / 2) * (60000 / time_diff)
  //     = (count * 30000) / time_diff
  return (count * 30000) / time_diff;
}

int CalculateFanSpeed(int fan_id, float temperature) {
  auto it = m_SensorSettings.find(fan_id);
  if (it == m_SensorSettings.end()) {
    return 0; // Sensor not found
  }

  const auto &curve = it->second.fan_speed_curve;
  if (curve.empty()) {
    return 0; // No curve defined
  }

  // Find the first threshold the temperature is below or equal to
  for (const auto &point : curve) {
    if (temperature <= point.temperature_threshold) {
      return std::min(point.fan_duty_cycle, (int)it->second.max_duty);
    }
  }

  // If temp is higher than all thresholds, use the last (highest) speed
  return std::min(curve.back().fan_duty_cycle, (int)it->second.max_duty);
}
