#include "wifi_manager.h"
#include <esp_wifi.h> // Used for mpdu_rx_disable android workaround

void InitializeWifi() {
    if (systemSettings.setup_done && !systemSettings.offline_mode) {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // ESP32 specific to clear previous static IP config
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(systemSettings.hostname.c_str());
        WiFi.begin(systemSettings.ssid, systemSettings.password);
        WiFi.setSleep(false); // Disable WiFi sleep mode

        Serial.print("Connecting to WiFi");
        oledDisplay.println("Connecting to WiFi");
        oledDisplay.display();

        // Original loop from main.cpp:
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            oledDisplay.print(".");
            oledDisplay.display();
        }

        Serial.println("\nWiFi connected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(AP_LOCAL_IP, AP_GATEWAY_IP, AP_SUBNET_MASK);
        WiFi.softAP("WaKu-ctl", ""); // Empty password for open AP

        // Android captive portal workaround
        // This sequence is important for some Android devices to connect to the AP
        // without "no internet" issues preventing access to the configuration page.
        esp_wifi_stop();
        esp_wifi_deinit();
        wifi_init_config_t my_config = WIFI_INIT_CONFIG_DEFAULT();
        my_config.ampdu_rx_enable = false; // Key change for compatibility
        esp_wifi_init(&my_config);
        esp_wifi_start();
        // Note: The original code in main.cpp did not re-apply softAPConfig or softAP here.
        // We are preserving that behavior. If AP issues arise, consider re-applying:
        // WiFi.softAPConfig(AP_LOCAL_IP, kGatewayIp, kSubnetMask);
        // WiFi.softAP("WaKu-ctl", "");

        Serial.print("WiFi AP Mode. IP: ");
        Serial.println(AP_LOCAL_IP);
    }
}

void ScanWifiNetworks(JsonDocument& doc) {
    Serial.println("Scanning WiFi networks...");
    int n = WiFi.scanNetworks();
    Serial.printf("%d networks found.\n", n);
    
    // Ensure the "networks" key is a JsonArray
    JsonArray networksArray;
    if (doc["networks"].is<JsonArray>()) {
        networksArray = doc["networks"].as<JsonArray>();
    } else {
        networksArray = doc["networks"].to<JsonArray>();
    }
    
    for (int i = 0; i < n; ++i) {
        networksArray.add(WiFi.SSID(i));
    }
}

void InitializeNtpTime() {
    if (systemSettings.offline_mode) return;

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    sntp_sync_status_t sync_status;
    int retries = 0;

    Serial.print("Synchronizing time");
    do {
        sync_status = sntp_get_sync_status();
        if (sync_status == SNTP_SYNC_STATUS_COMPLETED) break;
        if (retries++ > 100) {
            Serial.println("\nNTP Sync failed. Check internet connection.");
            return;
        }
        Serial.print(".");
        delay(100);
    } while (sync_status != SNTP_SYNC_STATUS_COMPLETED);

    Serial.println("\nTime synchronized.");
}

