#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Forward declaration
class WiFiManager;

// AWS IoT Configuration
#define AWS_IOT_ENDPOINT "a1lgz1948lk3nw-ats.iot.ap-south-1.amazonaws.com"
#define AWS_IOT_PORT 8883
#define THING_NAME "Smart_Plug_1"

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 19800
#define DAYLIGHT_OFFSET_SEC 0

// TOPICS
#define SHADOW_UPDATE_TOPIC "$aws/things/" THING_NAME "/shadow/update"
#define SHADOW_UPDATE_DELTA "$aws/things/" THING_NAME "/shadow/update/delta"
#define MQTT_PUB_TOPIC "smartplug/telemetry"
#define MQTT_SUB_TOPIC "smartplug/control"

// OTA Topics
#define OTA_PROGRESS_TOPIC "smartplug/ota/progress"

// OTA State Structure
struct OTAState {
    String fwVersion;           // Current firmware version
    String status;             // Normal/Downloading/Updating/Success/Failed
    String partition;          // Running partition
    String pendingVersion;     // Version waiting to install
    int progress;             // 0-100%
    uint32_t retryCount;      // Failed attempt counter
    
    OTAState() : 
        fwVersion("1.0.0"),
        status("Normal"),
        partition("app0"),
        pendingVersion(""),
        progress(0),
        retryCount(0) {}
};

// Shadow state structure (YOUR EXISTING STRUCT + NEW OTA FIELDS)
struct ShadowState {
    bool power : 1;
    bool overload_protection : 1;
    bool energy_monitoring : 1;
    float voltage_reading;
    float current_reading;
    float power_reading;
    float energy_total;
    float temperature;
    time_t last_wake_up_time;
    time_t last_reset_timestamp;
    unsigned long connection_attempts;
    
    // --- Your BMP/URL fields (KEEP) ---
    String url;
    String image_url;
    String download_status;
    int last_sent_index;
    
    // --- NEW: OTA fields ---
    OTAState ota;
    
    ShadowState() : 
        power(false),
        overload_protection(true),
        energy_monitoring(true),
        voltage_reading(0.0),
        current_reading(0.0),
        power_reading(0.0),
        energy_total(0.0),
        temperature(0.0),
        last_wake_up_time(0),
        last_reset_timestamp(0),
        connection_attempts(0),
        url(""),
        image_url(""),
        download_status("idle"),
        last_sent_index(0) {}
};

// AWS IoT Certificates
extern const char AWS_CERT_CA[];
extern const char AWS_CERT_CRT[];
extern const char AWS_CERT_PRIVATE[];

class MQTTManager {
public:
    MQTTManager(WiFiManager* wifiMgr);
    
    // Core functions
    bool begin();
    bool connect();
    void loop();
    void disconnect();
    bool isConnected() { return mqttClient.connected(); }
    
    // Time synchronization
    bool syncTime();
    time_t getCurrentTime();
    
    // Publishing methods
    bool publishTelemetry(const char* jsonPayload);
    bool updateShadow(float voltage, float current, float power, 
                     float energy, float temp, bool relayStatus);
    
    // OTA Status Publishing
    bool publishOTAProgress(int percent, const String& status, const String& version);
    
    //  Public publish method 
    bool publish(const char* topic, const char* payload);
    
    // Get current shadow state
    const ShadowState& getShadowState() const { return currentShadowState; }
    ShadowState& getShadowState() { return currentShadowState; }
    bool isShadowInitialized() const { return shadowInitialized; }
    
    // Callback setters 
    void setRelayCallback(void (*cb)(bool)) { relayCallback = cb; }
    void setResetCallback(void (*cb)()) { resetCallback = cb; }
    void setEnergyResetCallback(void (*cb)()) { energyResetCallback = cb; }
    void setShadowUpdateCallback(void (*cb)(const ShadowState&)) { shadowUpdateCallback = cb; }
    
    // OTA Callback 
    void setOTACallback(void (*cb)(JsonObject manifest)) { otaCallback = cb; }

private:
    WiFiClientSecure wifiClient;
    PubSubClient mqttClient;
    ShadowState currentShadowState;
    WiFiManager* wifiManager;
    
    // Callbacks
    void (*relayCallback)(bool state) = nullptr;
    void (*resetCallback)() = nullptr;
    void (*energyResetCallback)() = nullptr;
    void (*shadowUpdateCallback)(const ShadowState& state) = nullptr;
    
    // OTA Callback
    void (*otaCallback)(JsonObject manifest) = nullptr;
    
    // Timing
    uint32_t lastReconnectAttempt = 0;
    time_t lastTimeSync = 0;
    static const uint32_t RECONNECT_INTERVAL = 5000;
    static const uint32_t TIME_SYNC_INTERVAL = 3600000;
    
    bool shadowInitialized = false;
    
    // Static callback handler
    static void messageCallback(char* topic, byte* payload, unsigned int length);
    
    // Private methods
    bool attemptConnection();
    void subscribeToTopics();
    void handleMessage(char* topic, byte* payload, unsigned int length);
    void handleShadowDelta(JsonObject delta);
    void handleControlMessage(JsonObject& doc);
    
   
};

#endif