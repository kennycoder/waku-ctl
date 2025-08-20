#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "globals.h" // For gSettings, display, AP_LOCAL_IP, kGatewayIp, kSubnetMask, WiFi object
#include "ArduinoJson.h" // For JsonDocument used in ScanWifiNetworks

void InitializeWifi();
void ScanWifiNetworks(JsonDocument& doc); // Helper for the /networks endpoint
void InitializeNtpTime();

#endif // WIFI_MANAGER_H