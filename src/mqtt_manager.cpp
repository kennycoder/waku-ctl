#include "mqtt_manager.h"
#include "config_constants.h" // For kDebugEnabled, kMqttDebugEnabled, MQTT constants

// PrepareTelemetryPayload is defined in main.cpp (or another module) and will be linked.
extern std::string PrepareTelemetryPayload(const std::string& event = "default");

void InitializeMqttClient() {
    if (systemSettings.offline_mode || !systemSettings.mqtt_enable) {
        Serial.println("MQTT client initialization skipped (offline mode or MQTT disabled).");
        return;
    }

    mqttClient.setServer(systemSettings.mqtt_broker.c_str(), systemSettings.mqtt_port);
    mqttClient.setCallback(MqttCallback); // MqttCallback is now in this file
    mqttClient.setBufferSize(MQTT_CLIENT_BUFFER_SIZE); 
    mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT); 

    String client_id = espChipIdStr; 
    unsigned long mqtt_connect_start_time = millis();
    const unsigned long mqtt_connect_timeout_ms = 30000; // 30 seconds timeout for initial connection

    Serial.printf("Attempting to connect to MQTT broker: %s:%d as client ID: %s\n", 
                  systemSettings.mqtt_broker.c_str(), systemSettings.mqtt_port, client_id.c_str());

    while (!mqttClient.connected()) {
        if (millis() - mqtt_connect_start_time > mqtt_connect_timeout_ms) {
            Serial.println("MQTT connection timed out after 30 seconds during initialization. Will retry in background via loop.");
            return; 
        }
        Serial.print("Connecting to MQTT...");
        if (mqttClient.connect(client_id.c_str(), systemSettings.mqtt_username.c_str(), systemSettings.mqtt_password.c_str())) {
            Serial.println(" Connected.");
            // Example: mqttClient.subscribe("waku-ctl/command");
        } else {
            Serial.print(" Failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(". Retrying in 2s...");
            delay(2000); 
        }
    }
}

void MqttCallback(char* topic, byte* payload, unsigned int length) {
    if (DEBUG_MQTT_ENABLED) {
        Serial.print("MQTT Message [");
        Serial.print(topic);
        Serial.print("]: ");
        std::string payloadStr = "";
        for (unsigned int i = 0; i < length; i++) {
            payloadStr += (char)payload[i];
        }
        Serial.print(payloadStr.c_str());
        Serial.println("\n-----------------------");
    }
    // TODO: Add MQTT command handling here if needed
}

void SendMqttTelemetry() {
    if (!mqttClient.connected()) {
        if (DEBUG_ENABLED) Serial.println("MQTT: Client not connected. Skipping telemetry.");
        return;
    }
    if (DEBUG_ENABLED) Serial.println("Preparing MQTT telemetry...");
    std::string payload = PrepareTelemetryPayload("auto_mqtt"); 
    bool published = mqttClient.publish(systemSettings.mqtt_topic.c_str(), payload.c_str());

    if (DEBUG_ENABLED) {
        Serial.printf("MQTT: Payload to %s (%d bytes): %s\n", systemSettings.mqtt_topic.c_str(), payload.length(), payload.c_str());
        Serial.printf("MQTT: Publish call %s.\n", published ? "succeeded (queued)" : "failed (buffer full or other issue)");
    } else {
         Serial.printf("MQTT: %d bytes %s to %s\n", payload.length(), published ? "published" : "failed", systemSettings.mqtt_topic.c_str());
    }
}

void LoopMqttClient() {
    if (!systemSettings.offline_mode && systemSettings.mqtt_enable) {
        if (!mqttClient.connected()) {
            static unsigned long last_mqtt_reconnect_attempt = 0;
            if (millis() - last_mqtt_reconnect_attempt > 5000) { // Attempt to reconnect every 5 seconds
                last_mqtt_reconnect_attempt = millis();
                Serial.println("MQTT: Client disconnected. Attempting to reconnect...");
                InitializeMqttClient(); // Re-run the connection logic
            }
        }
        mqttClient.loop(); // Handles keep-alives and processes incoming messages
    }
}

void InitializeMqttTelemetryTask(Scheduler& scheduler, Task*& telemetryTaskRef) {
    if (!systemSettings.offline_mode && systemSettings.mqtt_enable && systemSettings.telemetry_interval > 0) {
        if (telemetryTaskRef != nullptr) { // If task exists, delete it before creating a new one
            delete telemetryTaskRef;
            telemetryTaskRef = nullptr;
        }
        telemetryTaskRef = new Task(systemSettings.telemetry_interval * TASK_MILLISECOND, TASK_FOREVER, &SendMqttTelemetry, &scheduler, true);
        if (telemetryTaskRef) {
            Serial.printf("MQTT Telemetry Task enabled. Interval: %d ms.\n", systemSettings.telemetry_interval);
        } else {
            Serial.println("Error: Failed to create MQTT Telemetry Task.");
        }
    } else {
        if (telemetryTaskRef != nullptr) {
            Serial.println("MQTT Telemetry Task being disabled or interval invalid. Deleting existing task.");
            delete telemetryTaskRef;
            telemetryTaskRef = nullptr;
        }
        if (DEBUG_ENABLED) Serial.println("MQTT Telemetry Task not started (offline, MQTT disabled, or interval invalid).");
    }
}