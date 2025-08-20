#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "globals.h"      // For gSettings, mqttClient, gEspChipIdStr, kMqttDebugEnabled, kDebugEnabled etc.
#include <TaskSchedulerDeclarations.h> // For Scheduler and Task types

void InitializeMqttClient();
void MqttCallback(char* topic, byte* payload, unsigned int length);
void SendMqttTelemetry(); // Internally calls PrepareTelemetryPayload
void LoopMqttClient();
void InitializeMqttTelemetryTask(Scheduler& scheduler, Task*& telemetryTaskRef);

#endif // MQTT_MANAGER_H