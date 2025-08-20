#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include "globals.h"

void PlayLedEffect(uint8_t led_strip_index);
void SwapLedChannel(LedChannel channel, int led_strip_index);
void SetStaticColor(uint8_t led_strip_index, uint8_t num_leds, int32_t color);
void SetGradientWave(uint8_t led_strip_index, uint8_t num_leds, int32_t start_color, int32_t end_color, uint8_t speed);
void SetMovingGradient(uint8_t led_strip_index, uint8_t num_leds, int32_t start_color, int32_t end_color, uint8_t speed);
void SetRainbowWave(uint8_t led_strip_index, int num_leds, uint8_t speed, uint8_t delta_hue);
int32_t InterpolateColor(int32_t color1, int32_t color2, float ratio);

#endif