#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "time.h"

MQTTManager* mqttManagerInstance = nullptr;

// Amazon Root CA 1 (RSA 2048) 
const char AWS_CERT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)EOF";

// Device Certificate 
const char AWS_CERT_CRT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDWTCCAkGgAwIBAgIUeS5bQl76F70NiBlttQD8xVnNpMMwDQYJKoZIhvcNAQEL
BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g
SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDExNjEwNTIx
OFoXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0
ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKaTiYcMHsf/GxC5qnnq
vvPCALBgX1VCfJFlgHNdDDhMqwyPfABSOGReQMH75sUnPHLYCBYmDJ+Gc4ccS3s+
7UT04MnC8Qy9/BwKJ1aQO/ioY52Wa7v7YE11NfKMtoAV475P+KZclUIMiic5CjYC
k+r985qxC5y4K5PjHXDQst5JbXjhio9baC3CGNtSEFjGohSB/xhHYi9VExu36ECm
CV9ni/7DWNB4f30aJhLCvumZsDwCqVezmEtlF3QzubV/LgYVd45eFRBsjCrAJ1RD
g4R1a7ORa1GTKLkpfeobUttNvwOQacIiddkwo0KP6/6ZWrG6iRAcOKIH7VEfj17c
sXMCAwEAAaNgMF4wHwYDVR0jBBgwFoAUppu4wP2bXeqxGFkjqcAjJXO4yS4wHQYD
VR0OBBYEFFlJx3RZNCwKx5GaqwDJtjMTxA6vMAwGA1UdEwEB/wQCMAAwDgYDVR0P
AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQBtqgj8WzVSVdEjpnt/BfoSXwck
wlhJIzbexQlSpP/RBYVzciYq2jTseNiG06k3UEW+n/qUhG4fRd6lunseLPfjoYZn
1MTLh2HQw8ND2GW6TJICQ9idrNs2qZ+NCJL/9zN38AEYZf0TmOlzyZ7CTbx87fIY
TG+1VJmIrG2dQhRH3DMGDioIzZUFMm782d7EzP3/QKmapRaY1X5LTg4SrD3TJro4
6lhE2K6uru0fZ0x2e3vHI0Uga8H76MuSXbR5ss9NHx72Z+hE9a+PvFdG8l3aGc2A
baJwjyAuf4GgJjzSIEZ4O40qb3Vo8wIfDuO62abKws+txI3PNkEh7j1DkqVm
-----END CERTIFICATE-----
)EOF";

// Device Private Key 
const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAppOJhwwex/8bELmqeeq+88IAsGBfVUJ8kWWAc10MOEyrDI98
AFI4ZF5AwfvmxSc8ctgIFiYMn4ZzhxxLez7tRPTgycLxDL38HAonVpA7+KhjnZZr
u/tgTXU18oy2gBXjvk/4plyVQgyKJzkKNgKT6v3zmrELnLgrk+MdcNCy3klteOGK
j1toLcIY21IQWMaiFIH/GEdiL1UTG7foQKYJX2eL/sNY0Hh/fRomEsK+6ZmwPAKp
V7OYS2UXdDO5tX8uBhV3jl4VEGyMKsAnVEODhHVrs5FrUZMouSl96htS202/A5Bp
wiJ12TCjQo/r/plasbqJEBw4ogftUR+PXtyxcwIDAQABAoIBAGTzAqSiNsFTm+5t
5p+OIP0OtGYvcXb1HRLsZYUEfdRcukiZaDe1nFFPQYWOCJOwrJSY0YXCt2GyFK9r
+V6OizKACP6dMoJbXL8NdDukm4OdYQlu0ImS1RD8GJ6OokdLfMKoKnN/pkDp4ovU
qJiExWnjT0+PPg9TGa29NOlawRuuf8MkcvmS6RwFrawucN+7qjT6htiAUdt7Nk+F
ibnfM7Sws/VdmA8Rqq4Z411Y8i25FAOoeiRNh12WYI0TM+3G43vDrAd4o1MoCIXo
QMncZgfBUd8JkTMnH6+ifABMcK384UbUgNFwCukK/x/dQKdnWUVJvETr82t0HIRG
kwBWhmkCgYEA1ecPTHxJHMFYfHNzTTtBlfNNQ8x1bJw9xHwC8EsjzkydnJyEnif0
0I73PtBhRFwt2r9l9CKIj87lWgEgoU6tUz+jvbjiBeSPG+5PvfvI1ay7WLT1Mu8B
rc2iO/dvOWyYy1aSNaRm1YvPfViE3Cb/uor5eP5SQqR+yI5hrxxLRK8CgYEAx1wQ
RjIgipwznJZNtcCg8BmfCJxqNwmS25dFRWnGY6fVk/9g+nPbB4AwphlkJNCt726Y
orDeJvKO7MvGLGivBv6GxARdmUG+2YMzS9fmsFEaC5EioqSBNL+ReEQ0z4szAb3u
9kVeKaarx9kWvpJheT3a6z96+Wq1OarxptmAWH0CgYAahne4NWVfon5VmH+A4UtF
zBEVykH5gPqL5hD7OWYsTAXziNlNP4k1X7U7Xd3h+0hYawm6l5m1s6NvYNpqBnap
7ydf/JBSyMASZ6AN4C5MiQoGexI5Cbh8lBZ9NzbcuSHNfWPOMR/rdVX6pkJ7hn6J
5HgBUBBlYT6zoixs6aZP0wKBgAyRvWTvnCWhG4/v2g5virYYp3I/imLV87BspS1v
MdbuqgSewVqJG3IpnueaCjpX/d9utajsRdVmzaQYZPI/12k1ewG41L3o60ODhiRu
BFlxg5bfG7Ptc0gEHAPdKQc824ZslzhnvzwZChObmFeDmymtwLO8WOCI3cw4/utq
IzFxAoGBALlyzOzTpPfOczZz6mhyRqYyZrBGvcT2ckF4NWEP1E3l2CzjMDQ5tOc3
2Lxk0FuxdiU62KBwSkSMkDmxLQtGpn9uGvoCzw5O9aHhW9JtejwVNllbiS4B6+io
VVS5zI8MCFFugjvpS1XVE9PweeamcHCXxZeNltO9tpl5B8r5vb4K
-----END RSA PRIVATE KEY-----
)EOF";

// ============================================================
// MQTT MANAGER IMPLEMENTATION
// ============================================================

MQTTManager::MQTTManager(WiFiManager* wifiMgr) : mqttClient(wifiClient), wifiManager(wifiMgr) {
    mqttManagerInstance = this;
}

bool MQTTManager::syncTime() {
    if (!wifiManager || !wifiManager->isConnected()) {
        return false;
    }
    
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    Serial.print("[TIME] Syncing time...");
    
    int timeout = 0;
    while (time(nullptr) < 1000000000 && timeout < 20) {
        Serial.print(".");
        delay(500);
        timeout++;
    }
    Serial.println();
    
    if (time(nullptr) < 1000000000) {
        Serial.println("[TIME] Failed to sync time");
        return false;
    }
    
    lastTimeSync = time(nullptr);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.printf("[TIME] Time synced: %s", asctime(&timeinfo));
    }
    
    return true;
}

time_t MQTTManager::getCurrentTime() {
    time_t now = time(nullptr);
    if (now < 1000000000) {
        return 0;
    }
    return now;
}

bool MQTTManager::begin() {
    Serial.print("Initializing AWS IoT connection... ");
    
    if (!syncTime()) {
        Serial.println("Time not synced, SSL may fail");
    }
    
    static char caCert[2048], deviceCert[2048], privateKey[2048];
    
    if (strlen_P(AWS_CERT_CA) > 10) {
        memcpy_P(caCert, AWS_CERT_CA, sizeof(caCert));
        wifiClient.setCACert(caCert);
    } else {
        Serial.println("Root CA not set!");
        return false;
    }
    
    if (strlen_P(AWS_CERT_CRT) > 10) {
        memcpy_P(deviceCert, AWS_CERT_CRT, sizeof(deviceCert));
        wifiClient.setCertificate(deviceCert);
    } else {
        Serial.println("Device certificate not set!");
        return false;
    }
    
    if (strlen_P(AWS_CERT_PRIVATE) > 10) {
        memcpy_P(privateKey, AWS_CERT_PRIVATE, sizeof(privateKey));
        wifiClient.setPrivateKey(privateKey);
    } else {
        Serial.println("Private key not set!");
        return false;
    }
    
    mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    mqttClient.setCallback(messageCallback);
    mqttClient.setBufferSize(2048);
    mqttClient.setKeepAlive(15);
    mqttClient.setSocketTimeout(30);
    
    Serial.println("✓");
    
    currentShadowState.connection_attempts = 0;
    
    return true;
}

bool MQTTManager::attemptConnection() {
    if (!wifiManager || !wifiManager->isConnected()) {
        Serial.println("Cannot connect - WiFi not connected");
        return false;
    }
    
    time_t currentTime = time(nullptr);
    if (currentTime - lastTimeSync > 3600 || currentTime < 1000000000) {
        syncTime();
    }
    
    String clientId = String(THING_NAME) + "_" + String(random(0xffff), HEX);
    String lwtTopic = "device/" + String(THING_NAME) + "/state";
    
    Serial.print("Connecting to AWS IoT with LWT... ");
    
    unsigned long startTime = millis();
    bool connected = false;
    
    while (millis() - startTime < 10000) {
        if (mqttClient.connect(
            clientId.c_str(),
            nullptr,
            nullptr,
            lwtTopic.c_str(),
            1,
            true,
            "{\"state\":{\"reported\":{\"device_status\":{\"connected\":\"false\"}}}}"
        )) {
            connected = true;
            break;
        }
        delay(500);
    }
    
    if (connected) {
        Serial.println(" CONNECTED");
        Serial.printf("  MQTT State: %d\n", mqttClient.state());
        
        subscribeToTopics();
        
        shadowInitialized = false;
        
        String connectedMsg = "{\"state\":{\"reported\":{\"device_status\":{\"connected\":\"true\"}}}}";
        if (mqttClient.publish(lwtTopic.c_str(), connectedMsg.c_str(), true)) {
            Serial.println("Published connected: true");
        } else {
            Serial.println("Failed to publish connected status");
        }
        
        return true;
    } else {
        Serial.print("✗ FAILED! Error: ");
        Serial.println(mqttClient.state());
        return false;
    }
}

void MQTTManager::subscribeToTopics() {
    bool shadowSubscribed = mqttClient.subscribe(SHADOW_UPDATE_DELTA);
    bool controlSubscribed = mqttClient.subscribe(MQTT_SUB_TOPIC);
    
    if (shadowSubscribed) {
        Serial.printf("Subscribed to: %s\n", SHADOW_UPDATE_DELTA);
    }
    if (controlSubscribed) {
        Serial.printf("Subscribed to: %s\n", MQTT_SUB_TOPIC);
    }
}

bool MQTTManager::connect() {
    if (mqttClient.connected()) {
        return true;
    }
    
    currentShadowState.connection_attempts++;
    Serial.printf("[MQTT] Connection attempt #%lu\n", currentShadowState.connection_attempts);
    
    return attemptConnection();
}

void MQTTManager::disconnect() {
    if (mqttClient.connected()) {
        String lwtTopic = "device/" + String(THING_NAME) + "/state";
        String disconnectedMsg = "{\"state\":{\"reported\":{\"device_status\":{\"connected\":\"false\"}}}}";
        
        mqttClient.publish(lwtTopic.c_str(), disconnectedMsg.c_str(), true);
        delay(100);
        
        mqttClient.disconnect();
        Serial.println("Disconnected from AWS IoT (LWT published)");
    }
}

void MQTTManager::loop() {
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            if (wifiManager && wifiManager->isConnected() && !wifiManager->isInSetupMode()) {
                connect();
            }
        }
        return;
    }
    
    mqttClient.loop();
}

bool MQTTManager::publish(const char* topic, const char* payload) {
    if (!mqttClient.connected()) {
        Serial.printf("[PUBLISH FAILED] Not connected - Topic: %s\n", topic);
        return false;
    }
    
    bool success = mqttClient.publish(topic, payload);
    if (success) {
        Serial.printf("[PUBLISH] Topic: %s\n", topic);
    } else {
        Serial.printf("[PUBLISH FAILED] Topic: %s\n", topic);
    }
    return success;
}

bool MQTTManager::publishTelemetry(const char* jsonPayload) {
    return publish(MQTT_PUB_TOPIC, jsonPayload);
}

// --- NEW: Publish OTA Progress ---
bool MQTTManager::publishOTAProgress(int percent, const String& status, const String& version) {
    StaticJsonDocument<256> doc;
    
    doc["device_id"] = THING_NAME;
    doc["timestamp"] = String(getCurrentTime());
    doc["ota_status"] = status;
    doc["ota_progress"] = percent;
    doc["ota_version"] = version;
    
    char buffer[256];
    serializeJson(doc, buffer);
    
    return publish(OTA_PROGRESS_TOPIC, buffer);
}

bool MQTTManager::updateShadow(float voltage, float current, float power, 
                              float energy, float temp, bool relayStatus) {
    Serial.println("[Shadow] Updating shadow...");
    
    StaticJsonDocument<1024> doc;
    
    time_t currentTime = getCurrentTime();
    
    JsonObject state = doc.createNestedObject("state");
    JsonObject desired = state.createNestedObject("desired");
    desired["welcome"] = "aws-iot";
    desired["relay_status"] = relayStatus ? "true" : "false";
    
    JsonObject reported = state.createNestedObject("reported");
    reported["welcome"] = "aws-iot";
    
    // Device details
    JsonObject device_details = reported.createNestedObject("device_details");
    device_details["device_id"] = THING_NAME;
    if (wifiManager && wifiManager->isConnected()) {
        device_details["local_ip"] = wifiManager->getIPAddress();
        extern char wifi_ssid[32];
        device_details["wifi_ssid"] = wifi_ssid;
    } else {
        device_details["local_ip"] = "0.0.0.0";
        device_details["wifi_ssid"] = "disconnected";
    }
    
    // OTA section (YOUR EXISTING + NEW FIELDS)
    JsonObject ota = reported.createNestedObject("ota");
    ota["fwVersion"] = currentShadowState.ota.fwVersion;
    ota["status"] = currentShadowState.ota.status;
    ota["partition"] = currentShadowState.ota.partition;
    ota["progress"] = currentShadowState.ota.progress;
    
    // Device diagnosis
    JsonObject device_diagnosis = reported.createNestedObject("device_diagnosis");
    device_diagnosis["network"] = "WiFi";
    device_diagnosis["connection_attempt"] = String(currentShadowState.connection_attempts);
    device_diagnosis["timestamp"] = String(currentTime > 0 ? currentTime : 0);
    
    int last_reset_seconds = 0;
    if (currentShadowState.last_reset_timestamp > 0 && currentTime > 0) {
        last_reset_seconds = currentTime - currentShadowState.last_reset_timestamp;
        if (last_reset_seconds < 0) last_reset_seconds = 0;
    }
    device_diagnosis["last_reset"] = last_reset_seconds;
    
    // Device status
    JsonObject device_status = reported.createNestedObject("device_status");
    device_status["connected"] = wifiManager && wifiManager->isConnected() ? "true" : "false";
    if (wifiManager && wifiManager->isConnected()) {
        device_status["rssi"] = String(wifiManager->getRSSI());
    } else {
        device_status["rssi"] = "0";
    }
    
    // Meter details
    JsonObject meter_details = reported.createNestedObject("meter_details");
    meter_details["current_reading"] = String(roundf(current * 1000) / 1000.0, 3);
    meter_details["power_reading"] = String(roundf(power * 1000) / 1000.0, 3);
    meter_details["energy_total"] = String(roundf(energy * 1000) / 1000.0, 3);
    meter_details["voltage_reading"] = String(roundf(voltage * 1000) / 1000.0, 3);
    meter_details["temperature"] = String(roundf(temp * 1000) / 1000.0, 3);
    
    // Your BMP/URL fields (KEEP)
    reported["url"] = currentShadowState.url;
    reported["image_url"] = currentShadowState.image_url;
    reported["download_status"] = currentShadowState.download_status;
    reported["last_sent_index"] = currentShadowState.last_sent_index;
    reported["relay_status"] = relayStatus ? "true" : "false";
    
    // Update local state
    currentShadowState.voltage_reading = voltage;
    currentShadowState.current_reading = current;
    currentShadowState.power_reading = power;
    currentShadowState.energy_total = energy;
    currentShadowState.temperature = temp;
    currentShadowState.power = relayStatus;
    
    if (currentShadowState.last_wake_up_time == 0 && currentTime > 0) {
        currentShadowState.last_wake_up_time = currentTime;
    }
    
    char jsonBuffer[1024];
    serializeJson(doc, jsonBuffer);
    
    return publish(SHADOW_UPDATE_TOPIC, jsonBuffer);
}

// ===== MESSAGE HANDLING =====

void MQTTManager::messageCallback(char* topic, byte* payload, unsigned int length) {
    if (mqttManagerInstance) {
        mqttManagerInstance->handleMessage(topic, payload, length);
    }
}

void MQTTManager::handleMessage(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    
    Serial.printf("Message received on: %s\n", topic);
    Serial.printf("Payload: %s\n", message);
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.print("JSON Parse error: ");
        Serial.println(error.c_str());
        return;
    }
    
    if (strcmp(topic, SHADOW_UPDATE_DELTA) == 0) {
        if (doc.containsKey("state")) {
            JsonObject delta = doc["state"];
            handleShadowDelta(delta);
        }
    }
    else if (strcmp(topic, MQTT_SUB_TOPIC) == 0) {
        JsonObject jsonObj = doc.as<JsonObject>();
        handleControlMessage(jsonObj);
    }
}

// --- UPDATED: Handle Shadow Delta with OTA ---
void MQTTManager::handleShadowDelta(JsonObject delta) {
    static unsigned long lastDeltaTime = 0;
    
    if (millis() - lastDeltaTime < 500) {
        return;
    }
    
    lastDeltaTime = millis();
    
    bool stateChanged = false;
    
    if (!shadowInitialized) {
        shadowInitialized = true;
    }
    
    // Handle relay status
    if (delta.containsKey("relay_status")) {
        String relayStatusStr = delta["relay_status"].as<String>();
        bool newPower = (relayStatusStr == "true");
        if (newPower != currentShadowState.power) {
            currentShadowState.power = newPower;
            stateChanged = true;
            Serial.printf("Shadow delta - Power: %s\n", newPower ? "ON" : "OFF");
            
            if (relayCallback != nullptr) {
                relayCallback(newPower);
            }
        }
    }
    
    // Handle reset energy
    if (delta.containsKey("reset_energy")) {
        String resetEnergyStr = delta["reset_energy"].as<String>();
        bool resetEnergy = (resetEnergyStr == "true");
        if (resetEnergy) {
            Serial.println("Shadow delta - Energy reset command");
            
            time_t currentTime = getCurrentTime();
            if (currentTime > 0) {
                currentShadowState.last_reset_timestamp = currentTime;
            } else {
                currentShadowState.last_reset_timestamp = millis() / 1000;
            }
            currentShadowState.energy_total = 0.0;
            
            if (energyResetCallback != nullptr) {
                energyResetCallback();
            }
            
            stateChanged = true;
        }
    }
    
    // --- NEW: Handle OTA Manifest ---
    if (delta.containsKey("ota_manifest")) {
        JsonObject manifest = delta["ota_manifest"];
        
        String newVersion = manifest["latest_version"] | "";
        String firmwareUrl = manifest["firmware_url"] | "";
        String sha256 = manifest["sha256"] | "";
        size_t size = manifest["size"] | 0;
        
        // Validate manifest
        if (newVersion.length() > 0 && 
            firmwareUrl.length() > 0 && 
            sha256.length() == 64 && 
            size > 0) {
            
            Serial.printf("[OTA] Received manifest for version: %s\n", newVersion.c_str());
            
            // Store pending version
            currentShadowState.ota.pendingVersion = newVersion;
            
            // Trigger OTA callback
            if (otaCallback != nullptr) {
                otaCallback(manifest);
            }
        }
    }
    
    // Handle your BMP/URL fields (KEEP)
    if (delta.containsKey("url")) {
        currentShadowState.url = delta["url"].as<String>();
        Serial.printf("Shadow delta - URL: %s\n", currentShadowState.url.c_str());
    }
    
    if (delta.containsKey("image_url")) {
        currentShadowState.image_url = delta["image_url"].as<String>();
        Serial.printf("Shadow delta - Image URL: %s\n", currentShadowState.image_url.c_str());
    }
    
    if (stateChanged && shadowUpdateCallback != nullptr) {
        shadowUpdateCallback(currentShadowState);
    }
}

void MQTTManager::handleControlMessage(JsonObject& doc) {
    static unsigned long lastControlTime = 0;
    
    if (millis() - lastControlTime < 500) {
        return;
    }
    
    if (doc.containsKey("relay_state")) {
        bool relayState = doc["relay_state"];
        Serial.printf("Control message - Relay: %s\n", relayState ? "ON" : "OFF");
        
        if (relayCallback != nullptr) {
            lastControlTime = millis();
            relayCallback(relayState);
        }
    }
    
    if (doc.containsKey("reset_energy")) {
        bool resetEnergy = doc["reset_energy"];
        if (resetEnergy && energyResetCallback != nullptr) {
            Serial.println("Control message - Energy reset");
            lastControlTime = millis();
            energyResetCallback();
        }
    }
}