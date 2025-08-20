#ifndef PERIPHERALS_MANAGER_H
#define PERIPHERALS_MANAGER_H

#include "globals.h"

unsigned long ReadFanRpm(int fan_index);
int CalculateFanSpeed(int fan_index, float temperature);

void Fan0TachIsr();
void Fan1TachIsr();
void Fan2TachIsr();
void Fan3TachIsr();

void InitializeAdc();
double ReadTemperature(int channel);
void InitializeOutputs();
void InitializeInputs();
void InitializeLeds();
void InitializeScreen();
    
int MapFanPercentToPwm(int percentage);
int MapValue(int value, int from_low, int from_high, int to_low, int to_high);

#endif // PERIPHERALS_MANAGER_H