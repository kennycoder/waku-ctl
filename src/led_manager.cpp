#include "led_manager.h"
#include <math.h>     // For M_PI in LED effects

void SwapLedChannel(LedChannel channel, int led_strip_index) {
    auto& settings = m_LedSettings[led_strip_index];

    if (settings.prev_mode == settings.mode) {
        // No change in mode, no need to swap channels
        return;
    } else {
        settings.prev_mode = settings.mode; // Update previous mode to current        
    }
    uint8_t int_pin = (led_strip_index == 0) ? PIN_LED_EXT_CTRL_1 : PIN_LED_EXT_CTRL_2;
    
#ifdef THREE_STATE_BUFFER_VERSION
    uint8_t tsb_pin = (led_strip_index == 0) ? PIN_LED_TSB_CTRL_1 : PIN_LED_TSB_CTRL_2;
#endif

    if (channel == LedChannel::Internal) {
        digitalWrite(int_pin, LOW);
#ifdef THREE_STATE_BUFFER_VERSION
        digitalWrite(tsb_pin, HIGH);
#endif
    } else { 
        digitalWrite(int_pin, HIGH);
#ifdef THREE_STATE_BUFFER_VERSION
        digitalWrite(tsb_pin, LOW);
#endif
    }
}

void PlayLedEffect(uint8_t led_strip_index) {
    auto& settings = m_LedSettings[led_strip_index];

    switch (settings.mode) {
        case 0: // Off
            SwapLedChannel(LedChannel::Internal, led_strip_index);
            SetStaticColor(led_strip_index, settings.num_leds, 0x000000);
            break;
        case 1: // Static Color
            SwapLedChannel(LedChannel::Internal, led_strip_index);
            SetStaticColor(led_strip_index, settings.num_leds, settings.start_color);
            break;
        case 2: // Gradient Wave
            SwapLedChannel(LedChannel::Internal, led_strip_index);
            SetGradientWave(led_strip_index, settings.num_leds, settings.start_color, settings.end_color, settings.speed);
            break;
        case 3: // Gradient Moving
            SwapLedChannel(LedChannel::Internal, led_strip_index);
            SetMovingGradient(led_strip_index, settings.num_leds, settings.start_color, settings.end_color, settings.speed);
            break;
        case 4: // Rainbow
            SwapLedChannel(LedChannel::Internal, led_strip_index);
            SetRainbowWave(led_strip_index, settings.num_leds, settings.speed, 50);
            break;
        case 5: // Passthrough (External)
            SwapLedChannel(LedChannel::External, led_strip_index);
            // No need to show, external signal takes over
            return; // Return early, no FastLED.show() needed for this strip
        default:
            break;
    }

    FastLED.show();

}

void SetStaticColor(uint8_t led_strip_index, uint8_t num_leds, int32_t color) {
    for (int i = 0; i < num_leds; i++) {
        a_LedBuffers[led_strip_index][i] = color;
    }
    for (int i = num_leds; i < MAX_LEDS_PER_STRIP; i++) {
        a_LedBuffers[led_strip_index][i] = 0;
    }  
}

int32_t InterpolateColor(int32_t color1, int32_t color2, float ratio) {
    CRGB c1(color1);
    CRGB c2(color2);
    CRGB blended = blend(c1, c2, static_cast<uint8_t>(ratio * 255.0f));
    return (blended.r << 16) | (blended.g << 8) | blended.b;
}

void SetGradientWave(uint8_t led_strip_index, uint8_t num_leds, int32_t start_color, int32_t end_color, uint8_t speed) {
    unsigned long current_time = millis();
    float time_factor = current_time * (speed * 0.00001f); // Adjust multiplier for desired speed range

    for (int i = 0; i < num_leds; i++) {
        float ratio = static_cast<float>(i) / (num_leds - 1);
        int32_t base_color = InterpolateColor(start_color, end_color, ratio);
        float wave = 0.5f + 0.5f * sin(2 * M_PI * (ratio + time_factor));

        CRGB c(base_color);
        c.r = static_cast<uint8_t>(c.r * wave);
        c.g = static_cast<uint8_t>(c.g * wave);
        c.b = static_cast<uint8_t>(c.b * wave);
        a_LedBuffers[led_strip_index][i] = c;
    }
}

void SetMovingGradient(uint8_t led_strip_index, uint8_t num_leds, int32_t start_color, int32_t end_color, uint8_t speed) {
    unsigned long current_time = millis();
    float offset = fmod(current_time * speed * 0.0001f, 1.0f); // Adjust multiplier

    for (int i = 0; i < num_leds; i++) {
        float position = static_cast<float>(i) / (num_leds - 1) + offset;
        if (position > 1.0f) position -= 1.0f;
        a_LedBuffers[led_strip_index][i] = InterpolateColor(start_color, end_color, position);
    }
}

void SetRainbowWave(uint8_t led_strip_index, int num_leds, uint8_t speed, uint8_t delta_hue) {
    uint8_t hue = beat8(speed, 255);
    fill_rainbow(a_LedBuffers[led_strip_index], num_leds, hue, delta_hue);
}

