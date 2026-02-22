#include <Arduino.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "ArduinoNvs.h"
#include "ADE9153A.h"
#include "ADE9153AAPI.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"

// ===== OTA Includes =====
#include "ota_manager.h"
#include <esp_ota_ops.h>
#include <esp_image_format.h>

// SPI Pin Definitions for ADE9153A
#define SPI_SCK_PIN   26    
#define SPI_MISO_PIN  25    
#define SPI_MOSI_PIN  27   
#define CS_PIN        12    
#define RESET_PIN     19    

// Relay Control Pin
#define RELAY_PIN     33

// LED Control Pins
#define STATUS_LED_PIN 22
#define LED_BLINK_INTERVAL 500

// Button for manual control and reset
#define BUTTON_PIN     23

// Zero-Crossing Detection Pin
#define ZC_PIN         21

// SPI Settings
#define SPI_SPEED     1000000

// Measurement interval
#define MEASUREMENT_INTERVAL_MS   100

// Storage namespace
#define OFFLINE_STORAGE_NS "meter_data"

// ===== OTA Constants =====
#define OTA_CHECK_INTERVAL_MS    86400000  // 24 hours
#define OTA_HEALTH_CHECK_TIME_MS 300000    // 5 minutes health check
#define FIRMWARE_VERSION        "1.2.0"    // Current firmware version

// Measurement averaging configuration
#define DEFAULT_AVERAGE_SAMPLES 3    
struct AveragingConfig {
    uint8_t num_samples = DEFAULT_AVERAGE_SAMPLES;  
    bool enabled = true;
};

// Calibration values
struct CalibrationData {
    float voltage_coefficient = 13.0068f;
    float current_coefficient = 0.36503161f;
    float power_coefficient = 0.66498695f;
    float energy_coefficient = 0.858307f;
    float current_offset = 0.019f;
};

// Raw measurement buffer
struct RawMeasurementsBuffer {
    int32_t raw_voltage_rms;
    uint32_t raw_current_rms;
    int32_t raw_active_power;
    int32_t raw_energy;
    
    RawMeasurementsBuffer() :  
        raw_voltage_rms(0), 
        raw_current_rms(0), 
        raw_active_power(0), 
        raw_energy(0) {}
};
 
// Measurement structure
struct Measurements {
    float voltage_rms;
    float current_rms;
    float active_power;
    float apparent_power;
    float reactive_power;
    float power_factor;
    float frequency;
    float temperature;
    float energy_wh;
    bool waveform_clipped;
    
    int32_t avg_raw_voltage_rms;
    uint32_t avg_raw_current_rms;
    int32_t avg_raw_active_power;
    int32_t avg_raw_energy;
    
    bool synchronized;
    unsigned long zc_timestamp;
    float voltage_at_zc;
    float current_at_zc;
};

// Global Variables
ADE9153AClass ade9153A;
Measurements measurements;
CalibrationData calibration;
AveragingConfig averagingConfig;
bool adeInitialized = false;
bool measurement_valid = false;

// Zero-Crossing Detection Variables
volatile bool zcDetected = false;
volatile unsigned long lastZCTime = 0;
volatile unsigned long lastZCPeriod = 0;
volatile unsigned long zcCounter = 0;
const unsigned long ZC_TIMEOUT_MS = 100;
bool zcSynchronizationEnabled = true;

// System state
bool relayState = false;

// Timing
unsigned long last_measurement_ms = 0;

// Energy tracking
float cumulativeEnergy = 0.0;  
unsigned long lastEnergyCalcTime = 0;

// WiFi and MQTT managers
WiFiManager wifiManager;
MQTTManager mqttManager(&wifiManager);

// LED status
bool ledState = false;
unsigned long lastLedBlink = 0;
bool ledShouldBlink = false;

// Button debouncing
bool lastButtonState = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Button hold for reset
bool buttonHeld = false;
unsigned long buttonPressStartTime = 0;

// Cloud publishing interval 
unsigned long lastPublishTime = 0;
const unsigned long PUBLISH_INTERVAL = 1000;  

// Offline storage
unsigned long lastStorageSave = 0;
const unsigned long STORAGE_SAVE_INTERVAL = 300000;
bool wasConnected = true;
unsigned long offlineStartTime = 0;

// Debug tracking
unsigned long lastDebugPrint = 0;
const unsigned long DEBUG_INTERVAL = 5000;

// System uptime tracking
unsigned long systemStartTime = 0;

// Averaging buffer
RawMeasurementsBuffer* rawBuffer = nullptr;
uint8_t bufferIndex = 0;
bool bufferReady = false;

// ===== OTA Variables =====
OTAManifest pendingManifest;
bool otaUpdatePending = false;
unsigned long lastOTACheck = 0;
bool otaHealthCheckPassed = false;
unsigned long otaBootTime = 0;

// OTA flags (declared in ota_manager.h)
extern volatile bool isOTARunning;
extern volatile bool isOTADownloading;
extern volatile bool isOTAUpdating;

// Function Prototypes
bool initializeADE9153A();
bool readRawMeasurement(RawMeasurementsBuffer& raw);
bool readMeasurements();
void calculateMeasurements();
void updateEnergyAccumulation();
void validateMeasurements();
void printMeasurements();
void publishToCloudAndShadow();
void updateShadow();  
void controlRelay(bool state);
void toggleRelay();
void handleButtonPress();
void checkModeTransition();
void handleSetupMode();
void handleNormalMode();
void initializeMQTT();
void saveEnergyToNVS();
void loadEnergyFromNVS();
void saveOfflineData();
void checkConnectionStatus();
void syncSystemTime();
void initAveragingBuffer();
void cleanupAveragingBuffer();
void applyAveraging();

// ===== OTA Function Prototypes =====
void checkForUpdates();
void onOTAManifest(JsonObject manifest);
void onOTAProgress(int percent, size_t current, size_t total);
void updateOTAStatus(const String& status, const String& version = "", int progress = 0);
void handleRollbackOnBoot();
void reportFirmwareVersion();

// Zero-Crossing Functions
void IRAM_ATTR zeroCrossingISR();
bool waitForZeroCrossing(unsigned long timeout_ms = 100);
void syncMeasurementWithZC();
float calculateFrequencyFromZC();
void checkZCSynchronization();

// Shadow callback function
void onShadowUpdate(const ShadowState& state) {
    // Don't process shadow updates during OTA
    if (isOTARunning) {
        Serial.println("[Shadow] Skipping update - OTA in progress");
        return;
    }
    
    static unsigned long lastShadowUpdateTime = 0;
    
    if (millis() - lastShadowUpdateTime < 1000) {
        Serial.println("[Shadow] Skipping update - too frequent");
        return;
    }
    
    lastShadowUpdateTime = millis();
    
    Serial.println("[Shadow] Received shadow update callback");
    
    if (state.power != relayState) {
        Serial.printf("[Shadow] Applying power state: %s\n", state.power ? "ON" : "OFF");
        controlRelay(state.power);
    }
}

// LED control callback for WiFi manager
void ledControlCallback(bool state) {
    digitalWrite(STATUS_LED_PIN, state ? HIGH : LOW);
    ledState = state;
}

// Callback functions for MQTT commands
void relayStateCallback(bool state) {
    // Don't process relay commands during OTA
    if (isOTARunning) {
        Serial.println("[Relay] Ignoring command - OTA in progress");
        return;
    }
    
    static unsigned long lastRelayCallback = 0;
    
    if (millis() - lastRelayCallback < 500) {
        return;
    }
    
    lastRelayCallback = millis();
    
    Serial.print("Relay state changed via MQTT: ");
    Serial.println(state ? "ON" : "OFF");
    controlRelay(state);
    
    if (mqttManager.isConnected() && adeInitialized && !isOTARunning) {
        updateShadow();
    }
}

void resetEnergyCallback() {
    // Don't process reset during OTA
    if (isOTARunning) {
        Serial.println("[Energy] Ignoring reset - OTA in progress");
        return;
    }
    
    Serial.println("Energy reset via MQTT");
    cumulativeEnergy = 0.0;
    saveEnergyToNVS();
    
    ShadowState& shadowState = mqttManager.getShadowState();
    time_t currentTime = mqttManager.getCurrentTime();
    if (currentTime > 0) {
        shadowState.last_reset_timestamp = currentTime;
    } else {
        shadowState.last_reset_timestamp = millis() / 1000;
    }
    
    if (adeInitialized && !isOTARunning) {
        updateShadow();
    }
}

// ===== OTA Callback Functions =====

void onOTAManifest(JsonObject manifest) {
    // Don't process new manifest if OTA is already running
    if (isOTARunning) {
        Serial.println("[OTA] Ignoring manifest - OTA already in progress");
        return;
    }
    
    Serial.println("[OTA] Received manifest from shadow delta");
    
    // Parse manifest
    pendingManifest.latest_version = manifest["latest_version"].as<String>();
    pendingManifest.firmware_url = manifest["firmware_url"].as<String>();
    pendingManifest.sha256 = manifest["sha256"].as<String>();
    pendingManifest.size = manifest["size"].as<size_t>();
    pendingManifest.valid = true;
    
    // Get current version from NVS
    String currentVersion = loadFirmwareVersion();
    
    // Check if update is available
    if (isUpdateAvailable(currentVersion, pendingManifest.latest_version)) {
        Serial.printf("[OTA] Update available: %s -> %s\n", 
                     currentVersion.c_str(), 
                     pendingManifest.latest_version.c_str());
        
        otaUpdatePending = true;
        
        // Update shadow status
        updateOTAStatus("Downloading", pendingManifest.latest_version, 0);
    } else {
        Serial.println("[OTA] Already on latest version or newer");
        pendingManifest.valid = false;
    }
}

void onOTAProgress(int percent, size_t current, size_t total) {
    static unsigned long lastProgressUpdate = 0;
    
    // Update shadow every 2 seconds
    if (millis() - lastProgressUpdate > 2000) {
        lastProgressUpdate = millis();
        updateOTAStatus("Downloading", pendingManifest.latest_version, percent);
        
        // Also publish to OTA progress topic
        mqttManager.publishOTAProgress(percent, "downloading", pendingManifest.latest_version);
        
        Serial.printf("[OTA] Progress: %d%%\n", percent);
    }
}

void updateOTAStatus(const String& status, const String& version, int progress) {
    if (!mqttManager.isConnected()) return;
    
    // Update shadow state
    ShadowState& shadowState = mqttManager.getShadowState();
    shadowState.ota.status = status;
    shadowState.ota.fwVersion = version.length() > 0 ? version : loadFirmwareVersion();
    shadowState.ota.partition = getRunningPartitionInfo();
    shadowState.ota.progress = progress;
    
    // Update shadow with current measurements (skip during OTA)
    if (adeInitialized && measurement_valid && !isOTARunning) {
        mqttManager.updateShadow(
            measurements.voltage_rms,
            measurements.current_rms,
            measurements.active_power,
            cumulativeEnergy,
            measurements.temperature,
            relayState
        );
    }
    
    Serial.printf("[OTA] Shadow status updated: %s, Version: %s, Progress: %d%%\n", 
                 status.c_str(), shadowState.ota.fwVersion.c_str(), progress);
}

void checkForUpdates() {
    // Don't check for updates if OTA is running
    if (isOTARunning) {
        Serial.println("[OTA] Skipping update check - OTA in progress");
        return;
    }
    
    if (!mqttManager.isConnected() || otaUpdatePending || isOTARunning) {
        return;
    }
    
    Serial.println("[OTA] Checking for updates...");
    setOTAStatus(OTA_CHECKING);
    
    // Request shadow to trigger delta
    StaticJsonDocument<128> doc;
    doc["state"]["reported"]["ota_check"] = String(millis());
    
    char buffer[128];
    serializeJson(doc, buffer);
    mqttManager.publish(SHADOW_UPDATE_TOPIC, buffer);
    
    lastOTACheck = millis();
}

void handleRollbackOnBoot() {
    // Check if this is a rollback boot
    if (isRollbackBoot()) {
        Serial.println("[OTA] Detected rollback boot!");
        
        uint8_t attempts = loadOTAAttempt() + 1;
        saveOTAAttempt(attempts);
        Serial.printf("[OTA] OTA attempt #%d\n", attempts);
        
        // If too many failures, maybe stay on old version
        if (attempts >= 3) {
            Serial.println("[OTA] Too many OTA failures, resetting counter");
            clearOTAAttempts();
        }
        
        updateOTAStatus("Rollback", loadFirmwareVersion(), 0);
    } else {
        // Normal boot - check if we're in pending verify state
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                Serial.println("[OTA] New firmware booted, starting health check timer");
                otaHealthCheckPassed = false;
                otaBootTime = millis();
                updateOTAStatus("Verifying", loadFirmwareVersion(), 100);
            }
        }
    }
}

void reportFirmwareVersion() {
    if (!mqttManager.isConnected() || isOTARunning) return;
    
    String currentVersion = loadFirmwareVersion();
    String partition = getRunningPartitionInfo();
    
    ShadowState& shadowState = mqttManager.getShadowState();
    shadowState.ota.fwVersion = currentVersion;
    shadowState.ota.status = "Normal";
    shadowState.ota.partition = partition;
    shadowState.ota.progress = 100;
    
    // Update shadow
    if (adeInitialized && measurement_valid) {
        mqttManager.updateShadow(
            measurements.voltage_rms,
            measurements.current_rms,
            measurements.active_power,
            cumulativeEnergy,
            measurements.temperature,
            relayState
        );
    }
    
    Serial.printf("[OTA] Reported version: %s on %s\n", 
                 currentVersion.c_str(), partition.c_str());
}

// Zero-Crossing Interrupt Service Routine
void IRAM_ATTR zeroCrossingISR() {
    unsigned long currentTime = micros();
    
    if (lastZCTime > 0) {
        lastZCPeriod = currentTime - lastZCTime;
    }
    
    lastZCTime = currentTime;
    zcDetected = true;
    zcCounter++;
}

// Wait for zero-crossing with timeout
bool waitForZeroCrossing(unsigned long timeout_ms) {
    unsigned long startTime = millis();
    bool localZCDetected = false;
    
    while (millis() - startTime < timeout_ms) {
        if (zcDetected) {
            localZCDetected = true;
            zcDetected = false;
            break;
        }
        delayMicroseconds(100);
    }
    
    return localZCDetected;
}

// Synchronize measurement with zero-crossing
void syncMeasurementWithZC() {
    if (!zcSynchronizationEnabled || !adeInitialized) {
        return;
    }
    
    if (waitForZeroCrossing(50)) {
        measurements.synchronized = true;
        measurements.zc_timestamp = micros() - lastZCTime;
        delayMicroseconds(100);
    } else {
        measurements.synchronized = false;
        measurements.zc_timestamp = 0;
        checkZCSynchronization();
    }
}

// Calculate frequency from zero-crossing periods
float calculateFrequencyFromZC() {
    if (lastZCPeriod == 0) {
        return 0.0;
    }
    
    float frequency = 1000000.0 / (float)lastZCPeriod;
    
    if (frequency < 45.0 || frequency > 65.0) {
        return 0.0;
    }
    
    return frequency;
}

// Check zero-crossing synchronization status
void checkZCSynchronization() {
    static unsigned long lastZCCheck = 0;
    static unsigned long lastZCCount = 0;
    
    if (millis() - lastZCCheck > 10000) {
        lastZCCheck = millis();
        
        unsigned long currentCount = zcCounter;
        unsigned long zcEvents = currentCount - lastZCCount;
        lastZCCount = currentCount;
        
        unsigned long expectedZCEvents = 10 * 2 * 50;
        
        if (zcEvents < expectedZCEvents * 0.5) {
            Serial.printf("[ZC] Warning: Low zero-crossing count: %lu (expected ~%lu)\n", 
                         zcEvents, expectedZCEvents);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    systemStartTime = millis();
    Serial.println("New Version Booting...");
    Serial.println("\n═══════════════════════════════════════════");
    Serial.println("        SMART PLUG   ");
    Serial.println("═══════════════════════════════════════════");
    Serial.printf("Device ID: %s\n", THING_NAME);
    Serial.printf("Firmware: %s (OTA Enabled)\n", FIRMWARE_VERSION);

    // ===== Handle Rollback on Boot =====
    handleRollbackOnBoot();

    // Initialize NVS for system flags
    if (NVS.begin("system")) {
        uint8_t justSetup = NVS.getInt("justSetup", 0);
        if (justSetup) {
            Serial.println("SYSTEM: Just completed setup");
            NVS.setInt("justSetup", 0);
            NVS.commit();
        }
        NVS.close();
    }

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    relayState = false;
    Serial.println("RELAY: Initialized (OFF)");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("LED: Initialized");

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    Serial.println("BUTTON: Initialized");
    
    pinMode(ZC_PIN, INPUT);
    Serial.println("ZERO-CROSSING: Pin configured");

    // Load energy from NVS
    loadEnergyFromNVS();
    
    // ===== Ensure firmware version in NVS =====
    String savedVersion = loadFirmwareVersion();
    if (savedVersion.length() == 0 || savedVersion == "1.0.0") {
        saveFirmwareVersion(FIRMWARE_VERSION);
        Serial.printf("[OTA] Initial firmware version set to %s\n", FIRMWARE_VERSION);
    }
    
    wifiManager.setLedCallback(ledControlCallback);
    
    Serial.print("\nWIFI: Initializing... ");
    if (wifiManager.begin()) {
        Serial.println("CONNECTED");
        digitalWrite(STATUS_LED_PIN, HIGH);
        
        extern char wifi_ssid[32];
        extern char wifi_password[64];
        
        Serial.printf("Network: %s\n", wifi_ssid);
        Serial.printf("IP Address: %s\n", wifiManager.getIPAddress());
        Serial.printf("Signal: %d dBm\n", wifiManager.getRSSI());
    } else {
        Serial.println("SETUP MODE");
    }
    
    Serial.print("\nADE9153A: Initializing... ");
    if (!initializeADE9153A()) {
        Serial.println("FAILED!");
        ledShouldBlink = true;
    } else {
        Serial.println("READY");
    }
    
    mqttManager.setRelayCallback(relayStateCallback);
    mqttManager.setResetCallback(resetEnergyCallback);
    mqttManager.setEnergyResetCallback(resetEnergyCallback);
    mqttManager.setShadowUpdateCallback(onShadowUpdate);
    
    // ===== Set OTA Callback =====
    mqttManager.setOTACallback(onOTAManifest);
    
    if (wifiManager.isConnected() && !wifiManager.isInSetupMode()) {
        initializeMQTT();
    } else {
        Serial.println("MQTT: Initialization skipped");
    }
    
    // Initialize averaging buffer
    initAveragingBuffer();
    
    // Sync system time
    syncSystemTime();
    
    Serial.println("\nSYSTEM: Ready");
    Serial.println("═══════════════════════════════════════════");
    
    lastEnergyCalcTime = millis();
    lastPublishTime = millis();
    last_measurement_ms = millis();
    
    checkModeTransition();
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Don't process button during OTA
    if (!isOTARunning) {
        handleButtonPress();
    }
    
    wifiManager.handleClient();
    checkModeTransition();
    
    if (wifiManager.isInSetupMode()) {
        handleSetupMode();
    } else {
        // Don't handle normal mode during OTA
        if (!isOTARunning) {
            handleNormalMode();
            checkConnectionStatus();
        } else {
            // Just keep MQTT alive during OTA
            mqttManager.loop();
        }
        
        // ===== OTA Health Check =====
        if (!otaHealthCheckPassed && otaBootTime > 0 && !isOTARunning) {
            if (currentMillis - otaBootTime >= OTA_HEALTH_CHECK_TIME_MS) {
                if (markAppValid()) {
                    Serial.println("[OTA] Health check passed, firmware marked valid");
                    otaHealthCheckPassed = true;
                    clearOTAAttempts();
                    updateOTAStatus("Normal", "", 100);
                }
            }
        }
        
        // ===== Check for OTA updates periodically =====
        if (!otaUpdatePending && !isOTARunning) {
            if (lastOTACheck == 0 || (currentMillis - lastOTACheck >= OTA_CHECK_INTERVAL_MS)) {
                checkForUpdates();
            }
        }
        
        // ===== Process pending OTA update =====
        if (otaUpdatePending && !isOTARunning) {
            Serial.println("[OTA] Starting OTA update process...");
            
            // Disable zero-crossing interrupts during OTA
            detachInterrupt(digitalPinToInterrupt(ZC_PIN));
            
            // Perform OTA update
            bool success = downloadAndUpdate(pendingManifest, onOTAProgress);
            
            if (!success) {
                Serial.println("[OTA] Update failed");
                otaUpdatePending = false;
                updateOTAStatus("Failed", loadFirmwareVersion(), 0);
                
                // Re-enable zero-crossing
                attachInterrupt(digitalPinToInterrupt(ZC_PIN), zeroCrossingISR, RISING);
            }
            // If success, device will restart in downloadAndUpdate()
        }
    }
    
    // Skip measurements during OTA update phase (flashing)
    if (!isOTAUpdating) {
        // Check zero-crossing synchronization periodically
        checkZCSynchronization();
        
        // Measurement timing
        if (currentMillis - last_measurement_ms >= MEASUREMENT_INTERVAL_MS) {
            last_measurement_ms = currentMillis;
            
            if (adeInitialized) {
                if (zcSynchronizationEnabled) {
                    syncMeasurementWithZC();
                }
                
                if (readMeasurements()) {
                    calculateMeasurements();
                    updateEnergyAccumulation();
                    validateMeasurements();
                }
            }
        }
    }
    
    delay(10);
}

// AVERAGING FUNCTIONS
void initAveragingBuffer() {
    cleanupAveragingBuffer(); 
    
    if (averagingConfig.num_samples > 0) {
        rawBuffer = new RawMeasurementsBuffer[averagingConfig.num_samples];
        if (rawBuffer == nullptr) {
            Serial.println("Failed to allocate averaging buffer!");
            averagingConfig.enabled = false;
        } else {
            bufferIndex = 0;
            bufferReady = false;
        }
    }
}

void cleanupAveragingBuffer() {
    if (rawBuffer != nullptr) {
        delete[] rawBuffer;
        rawBuffer = nullptr;
    }
    bufferIndex = 0;
    bufferReady = false;
}

bool readRawMeasurement(RawMeasurementsBuffer& raw) {
    if (!adeInitialized) {
        return false;
    }
    
    raw.raw_voltage_rms = (int32_t)ade9153A.SPI_Read_32(REG_AVRMS_2);
    raw.raw_current_rms = ade9153A.SPI_Read_32(REG_AIRMS_2);
    raw.raw_active_power = (int32_t)ade9153A.SPI_Read_32(REG_AWATT);
    raw.raw_energy = (int32_t)ade9153A.SPI_Read_32(REG_AWATTHR_HI);
    
    uint32_t version_check = ade9153A.SPI_Read_32(REG_VERSION_PRODUCT);
    if (version_check != 0x0009153A) {
        Serial.printf("Chip verification failed: 0x%08lX\n", version_check);
        return false;
    }
    
    return true;
}

void applyAveraging() {
    if (!averagingConfig.enabled || averagingConfig.num_samples < 1) {
        if (bufferIndex > 0) {
            measurements.avg_raw_voltage_rms = rawBuffer[0].raw_voltage_rms;
            measurements.avg_raw_current_rms = rawBuffer[0].raw_current_rms;
            measurements.avg_raw_active_power = rawBuffer[0].raw_active_power;
            measurements.avg_raw_energy = rawBuffer[0].raw_energy;
        }
        return;
    }
    
    int64_t sum_voltage = 0;
    uint64_t sum_current = 0;
    int64_t sum_power = 0;
    int64_t sum_energy = 0;
    
    uint8_t samples_to_average = bufferReady ? averagingConfig.num_samples : bufferIndex;
    
    if (samples_to_average == 0) {
        return;
    }
    
    for (uint8_t i = 0; i < samples_to_average; i++) {
        sum_voltage += rawBuffer[i].raw_voltage_rms;
        sum_current += rawBuffer[i].raw_current_rms;
        sum_power += rawBuffer[i].raw_active_power;
        sum_energy += rawBuffer[i].raw_energy;
    }
    
    measurements.avg_raw_voltage_rms = sum_voltage / samples_to_average;
    measurements.avg_raw_current_rms = sum_current / samples_to_average;
    measurements.avg_raw_active_power = sum_power / samples_to_average;
    measurements.avg_raw_energy = sum_energy / samples_to_average;
}

// ============================================================================
// ADE9153A FUNCTIONS
// ============================================================================
bool initializeADE9153A() {
    Serial.println("\n[ADE9153A] Initializing with Zero-Crossing Detection...");
    
    pinMode(RESET_PIN, OUTPUT);
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(RESET_PIN, HIGH);
    digitalWrite(CS_PIN, HIGH);
    
    digitalWrite(RESET_PIN, LOW);
    delay(10);
    digitalWrite(RESET_PIN, HIGH);
    delay(100);
    
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, CS_PIN);
    
    if (!ade9153A.SPI_Init(SPI_SPEED, CS_PIN)) {
        Serial.println("SPI Initialization failed!");
        return false;
    }
    
    Serial.println("Configuring zero-crossing detection...");
    
    ade9153A.SPI_Write_16(REG_CFMODE, 0x0001);
    ade9153A.SPI_Write_16(REG_ZX_CFG, 0x0001);
    ade9153A.SPI_Write_16(REG_ZXTHRSH, 0x000A);
    ade9153A.SPI_Write_16(REG_ZXTOUT, 0x03E8);
    
    ade9153A.SetupADE9153A();
    ade9153A.SPI_Write_16(REG_AI_PGAGAIN, 0x000A);
    ade9153A.SPI_Write_32(REG_CONFIG0, 0);
    ade9153A.SPI_Write_16(REG_EP_CFG, ADE9153A_EP_CFG);
    ade9153A.SPI_Write_16(REG_EGY_TIME, ADE9153A_EGY_TIME);
    ade9153A.SPI_Write_32(REG_AVGAIN, 0xFFF36B16);
    ade9153A.SPI_Write_32(REG_AIGAIN, 7316126);
    ade9153A.SPI_Write_16(REG_PWR_TIME, 3906);
    ade9153A.SPI_Write_16(REG_TEMP_CFG, 0x000C);
    ade9153A.SPI_Write_16(REG_COMPMODE, 0x0005);
    delay(10);
    
    ade9153A.SPI_Write_16(REG_RUN, 0x0001);
    delay(500);
    
    uint32_t version = ade9153A.SPI_Read_32(REG_VERSION_PRODUCT);
    if (version != 0x0009153A) {
        Serial.printf("ADE9153A not detected. ID: 0x%lX\n", version);
        return false;
    }
    
    Serial.printf("ADE9153A detected. Product ID: 0x%lX\n", version);
    
    attachInterrupt(digitalPinToInterrupt(ZC_PIN), zeroCrossingISR, RISING);
    Serial.println("Zero-crossing interrupt enabled");
    
    memset(&measurements, 0, sizeof(measurements));
    adeInitialized = true;
    
    return true;
}

bool readMeasurements() {
    if (!adeInitialized || rawBuffer == nullptr) {
        return false;
    }
    
    if (!readRawMeasurement(rawBuffer[bufferIndex])) {
        measurement_valid = false;
        return false;
    }
    
    bufferIndex++;
    
    if (bufferIndex >= averagingConfig.num_samples) {
        bufferIndex = 0;
        bufferReady = true;
    }
    
    applyAveraging();
    
    measurement_valid = true;
    
    return true;
}

void calculateMeasurements() {
    if (!measurement_valid) {
        return;
    }
    
    measurements.voltage_rms = (float)measurements.avg_raw_voltage_rms * 
                               calibration.voltage_coefficient / 1000000.0f;
    
    measurements.current_rms = (float)measurements.avg_raw_current_rms * 
                               calibration.current_coefficient / 1000000.0f;
    
    if (measurements.current_rms < 0.5f) {
        measurements.current_rms += calibration.current_offset;
    }
    
    float raw_power = fabs((float)measurements.avg_raw_active_power);
    measurements.active_power = raw_power * calibration.power_coefficient / 1000.0f;
    
    measurements.apparent_power = measurements.voltage_rms * 
                                  measurements.current_rms;
    
    if (measurements.apparent_power > 0.1f) {
        measurements.reactive_power = sqrtf(
            measurements.apparent_power * measurements.apparent_power - 
            measurements.active_power * measurements.active_power
        );
    } else {
        measurements.reactive_power = 0.0f;
    }
    
    float zc_frequency = calculateFrequencyFromZC();
    if (zc_frequency > 0.0) {
        measurements.frequency = zc_frequency;
    } else {
        uint32_t period_raw = ade9153A.SPI_Read_32(REG_APERIOD);
        if (period_raw > 0) {
            measurements.frequency = (4000.0f * 65536.0f) / (float)(period_raw + 1);
        } else {
            measurements.frequency = 0.0f;
        }
    }
    
    int32_t powerFactor_raw = ade9153A.SPI_Read_32(REG_APF);
    measurements.power_factor = fabs((float)powerFactor_raw / 134217728.0f);
    
    uint32_t trim = ade9153A.SPI_Read_32(REG_TEMP_TRIM);
    uint16_t gain = trim & 0xFFFF;
    uint16_t offset = (trim >> 16) & 0xFFFF;
    uint16_t temp_raw = ade9153A.SPI_Read_16(REG_TEMP_RSLT);
    measurements.temperature = ((float)offset / 32.0f) - 
                               ((float)temp_raw * (float)gain / 131072.0f);
    
    measurements.waveform_clipped = (abs(measurements.avg_raw_voltage_rms) > 8000000) || 
                                   (measurements.avg_raw_current_rms > 8000000);
}

void updateEnergyAccumulation() {
    unsigned long currentTime = millis();
    
    if (lastEnergyCalcTime == 0) {
        lastEnergyCalcTime = currentTime;
        return;
    }
    
    float delta_hours = (float)(currentTime - lastEnergyCalcTime) / 3600000.0f;
    float energy_increment = measurements.active_power * delta_hours;
    
    if (energy_increment > 0 && relayState) {
        cumulativeEnergy += energy_increment;
        measurements.energy_wh = cumulativeEnergy;
        
        static float lastSavedEnergy = 0;
        if (fabs(cumulativeEnergy - lastSavedEnergy) > 0.1 || 
            currentTime - lastStorageSave > 300000) {
            saveEnergyToNVS();
            lastSavedEnergy = cumulativeEnergy;
            lastStorageSave = currentTime;
        }
    }
    
    lastEnergyCalcTime = currentTime;
}

void validateMeasurements() {
    if (measurements.voltage_rms > 300.0f) {
        measurement_valid = false;
        return;
    }
    
    if (measurements.current_rms < 0.0f || measurements.current_rms > 100.0f) {
        measurement_valid = false;
        return;
    }
    
    if (measurements.frequency < 45.0 || measurements.frequency > 65.0) {
        measurement_valid = false;
        return;
    }
    
    measurement_valid = true;
}

void printMeasurements() {
    static uint32_t measurementCount = 0;
    
    Serial.println("\n═══════════════════════════════════════════");
    Serial.printf("         MEASUREMENT #%d\n", ++measurementCount);
    Serial.println("═══════════════════════════════════════════");
    
    Serial.println("SYSTEM STATUS");
    Serial.printf("   Relay:        %s\n", relayState ? "ON " : "OFF ");
    Serial.printf("   Temperature:  %.1f°C\n", measurements.temperature);
    Serial.printf("   Frequency:    %.2f Hz\n", measurements.frequency);
    
    Serial.println("\nPOWER MEASUREMENTS");
    Serial.printf("   Voltage:      %7.3f V\n", measurements.voltage_rms);
    Serial.printf("   Current:      %7.3f A\n", measurements.current_rms);
    Serial.printf("   Power (Active): %6.3f W\n", measurements.active_power);
    
    if (measurements.reactive_power > 0.1) {
        Serial.printf("   Power (Reactive): %5.3f VAR\n", measurements.reactive_power);
    }
    
    if (measurements.apparent_power > 0.1) {
        Serial.printf("   Power (Apparent): %5.3f VA\n", measurements.apparent_power);
    }
    
    Serial.println("\nENERGY & QUALITY");
    Serial.printf("   Energy Total:  %.3f Wh\n", cumulativeEnergy);
    Serial.printf("   Power Factor:  %.3f\n", measurements.power_factor);
    
    Serial.println("\nSTATUS INDICATORS");
    Serial.printf("   Waveform:     %s\n", measurements.waveform_clipped ? "CLIPPED" : "Clean");
    Serial.printf("   ZC Sync:      %s\n", measurements.synchronized ? " Synced" : " Pending");
    Serial.printf("   Valid Data:   %s\n", measurement_valid ? " Valid" : "Invalid");
    
    // ===== OTA Status in Debug =====
    Serial.printf("   OTA Status:   %s\n", getOTAStatusString(getOTAStatus()).c_str());
    Serial.printf("   Firmware:     %s\n", loadFirmwareVersion().c_str());
    
    Serial.println("═══════════════════════════════════════════");
}

// ============================================================================
// NVS STORAGE FUNCTIONS 
// ============================================================================
void saveEnergyToNVS() {
    if (!NVS.begin(OFFLINE_STORAGE_NS)) {
        Serial.println("[NVS] Failed to open storage");
        return;
    }
    
    bool success = NVS.setFloat("energy_total", cumulativeEnergy);
    success = success && NVS.setInt("relay_state", relayState ? 1 : 0);
    success = success && NVS.commit();
    
    NVS.close();
    
    if (success) {
        Serial.printf("[NVS] Saved: %.3f Wh, Relay: %s\n", 
                     cumulativeEnergy, relayState ? "ON" : "OFF");
    } else {
        Serial.println("[NVS] Failed to save energy");
    }
}

void loadEnergyFromNVS() {
    if (!NVS.begin(OFFLINE_STORAGE_NS)) {
        Serial.println("[NVS] Failed to open storage for reading");
        return;
    }
    
    cumulativeEnergy = NVS.getFloat("energy_total", 0.0);
    int storedRelay = NVS.getInt("relay_state", 0);
    relayState = (storedRelay == 1);
    
    NVS.close();
    
    Serial.printf("[NVS] Loaded: %.3f Wh, Relay: %s\n", 
                 cumulativeEnergy, relayState ? "ON" : "OFF");
    
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
}

void saveOfflineData() {
    if (!NVS.begin(OFFLINE_STORAGE_NS)) {
        return;
    }
    
    NVS.setFloat("last_voltage", measurements.voltage_rms);
    NVS.setFloat("last_current", measurements.current_rms);
    NVS.setFloat("last_power", measurements.active_power);
    NVS.setFloat("last_temp", measurements.temperature);
    NVS.setFloat("energy_total", cumulativeEnergy);
    NVS.setInt("relay_state", relayState ? 1 : 0);
    NVS.setInt("last_save", (int32_t)(millis() / 1000));
    
    NVS.commit();
    NVS.close();
}

// ============================================================================
// TIME SYNCHRONIZATION
// ============================================================================
void syncSystemTime() {
    Serial.print("Synchronizing system time... ");
    
    if (WiFi.status() == WL_CONNECTED) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
        int timeout = 0;
        Serial.print("Waiting");
        while (time(nullptr) < 1000000000 && timeout < 20) {
            Serial.print(".");
            delay(500);
            timeout++;
        }
        Serial.println();
        
        if (time(nullptr) < 1000000000) {
            Serial.println("[TIME] Failed to sync time");
            return;
        }
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.printf("[TIME] Time synced: %s", asctime(&timeinfo));
            
            if (mqttManager.isConnected()) {
                time_t currentTime = time(nullptr);
                if (currentTime > 0) {
                    ShadowState& shadowState = mqttManager.getShadowState();
                    shadowState.last_reset_timestamp = currentTime;
                    shadowState.last_wake_up_time = currentTime;
                }
            }
            return;
        }
    }
    
    Serial.println("False - WiFi not connected");
}

// ============================================================================
// SYSTEM FUNCTIONS
// ============================================================================
void handleButtonPress() {
    static unsigned long lastValidPress = 0;
    const unsigned long PRESS_COOLDOWN = 1000;
    
    int reading = digitalRead(BUTTON_PIN);
    
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != buttonState) {
            buttonState = reading;
            
            if (buttonState == LOW) {
                buttonPressStartTime = millis();
                buttonHeld = true;
            } else {
                unsigned long pressDuration = millis() - buttonPressStartTime;
                if (pressDuration < 1000 && pressDuration > 50) {
                    if (millis() - lastValidPress > PRESS_COOLDOWN) {
                        lastValidPress = millis();
                        Serial.println("Button pressed - Toggling relay");
                        toggleRelay();
                    }
                }
                buttonHeld = false;
                wifiManager.handleResetButton(false, 0);
            }
        }
    }
    
    if (buttonHeld) {
        unsigned long holdDuration = millis() - buttonPressStartTime;
        wifiManager.handleResetButton(true, holdDuration);
    }
    
    wifiManager.updateResetIndicator();
    lastButtonState = reading;
}

void handleSetupMode() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastLedBlink > LED_BLINK_INTERVAL) {
        lastLedBlink = currentMillis;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
    }
    
    if (currentMillis - lastDebugPrint > DEBUG_INTERVAL) {
        lastDebugPrint = currentMillis;
        if (adeInitialized && measurement_valid) {
            Serial.printf("Setup Mode - V:%.1fV I:%.3fA P:%.1fW Relay:%s\n",
                         measurements.voltage_rms,
                         measurements.current_rms,
                         measurements.active_power,
                         relayState ? "ON" : "OFF");
        }
    }
}

void handleNormalMode() {
    // Don't attempt WiFi reconnect during OTA
    if (!isOTARunning) {
        wifiManager.reconnect();
    }
    
    unsigned long currentMillis = millis();
    
    // Don't publish during OTA
    if (!isOTARunning && currentMillis - lastPublishTime > PUBLISH_INTERVAL) {
        if (wifiManager.isConnected() && mqttManager.isConnected()) {
            publishToCloudAndShadow();
            lastPublishTime = currentMillis;
        } else if (!wifiManager.isConnected()) {
            if (currentMillis - lastStorageSave > 30000) {
                saveOfflineData();
                lastStorageSave = currentMillis;
                Serial.println("[OFFLINE] Saved data locally");
            }
        }
    }
    
    // Debug prints - skip during OTA to avoid serial clutter
    if (!isOTARunning && currentMillis - lastDebugPrint > DEBUG_INTERVAL) {
        lastDebugPrint = currentMillis;
        Serial.print("\n[DEBUG] WiFi: ");
        Serial.print(wifiManager.isConnected() ? "Connected" : "Disconnected");
        Serial.print(" | MQTT: ");
        Serial.print(mqttManager.isConnected() ? "Connected" : "Disconnected");
        Serial.print(" | Relay: ");
        Serial.print(relayState ? "ON" : "OFF");
        Serial.print(" | ADE9153A: ");
        Serial.print(adeInitialized ? "OK" : "FAILED");
        Serial.print(" | OTA: ");
        Serial.print(getOTAStatusString(getOTAStatus()).c_str());
        Serial.print(" | FW: ");
        Serial.println(loadFirmwareVersion().c_str());
        
        if (wifiManager.isConnected()) {
            Serial.print("[DEBUG] RSSI: ");
            Serial.print(wifiManager.getRSSI());
            Serial.print(" dBm | IP: ");
            Serial.println(wifiManager.getIPAddress());
        }
    }
    
    // MQTT loop - still run during OTA to keep connection alive
    mqttManager.loop();
    
    // Print measurements - skip during OTA
    if (!isOTARunning) {
        static unsigned long lastPrintTime = 0;
        if (currentMillis - lastPrintTime > 3000) {
            lastPrintTime = currentMillis;
            printMeasurements();
        }
    }
}

void checkConnectionStatus() {
    static bool wasConnectedBefore = false;
    bool isConnectedNow = wifiManager.isConnected() && mqttManager.isConnected();
    
    if (!wasConnectedBefore && isConnectedNow) {
        Serial.println("\nBACK ONLINE! Updating cloud...");
        if (adeInitialized && !isOTARunning) {
            updateShadow();
            // ===== Report firmware version on reconnect =====
            reportFirmwareVersion();
        }
    }
    else if (wasConnectedBefore && !isConnectedNow) {
        Serial.println("\nOFFLINE: Saving data locally...");
        saveOfflineData();
    }
    
    wasConnectedBefore = isConnectedNow;
}

void initializeMQTT() {
    if (wifiManager.isConnected() && !wifiManager.isInSetupMode()) {
        Serial.print("MQTT: Initializing... ");
        if (mqttManager.begin()) {
            Serial.println("Success");
            
            Serial.print("MQTT: Connecting to AWS IoT... ");
            if (mqttManager.connect()) {
                Serial.println("CONNECTED");
                
                if (adeInitialized && !isOTARunning) {
                    Serial.println("MQTT: Performing initial publish...");
                    publishToCloudAndShadow();
                    
                    // ===== Report firmware version =====
                    reportFirmwareVersion();
                    
                    // ===== Check for updates on boot =====
                    lastOTACheck = millis() - OTA_CHECK_INTERVAL_MS + 10000; // Check after 10 seconds
                }
            } else {
                Serial.println("Failed");
            } 
        } else {
            Serial.println("Failed to initialize!");
        }
    }
}

void checkModeTransition() {
    static bool wasInSetupMode = true;
    bool currentSetupMode = wifiManager.isInSetupMode();
    
    if (currentSetupMode && !wasInSetupMode) {
        wasInSetupMode = true;
        Serial.println("\nENTERED SETUP MODE");
        mqttManager.disconnect();
        detachInterrupt(digitalPinToInterrupt(ZC_PIN));
    } else if (!currentSetupMode && wasInSetupMode) {
        wasInSetupMode = false;
        Serial.println("\nTRANSITIONED TO NORMAL MODE");
        digitalWrite(STATUS_LED_PIN, HIGH);
        
        attachInterrupt(digitalPinToInterrupt(ZC_PIN), zeroCrossingISR, RISING);
        
        initializeMQTT();
    }
}

void publishToCloudAndShadow() {
    if (!wifiManager.isConnected() || isOTARunning) {
        return;
    }
    
    if (!mqttManager.isConnected()) {
        if (mqttManager.connect()) {
            Serial.println("[PUBLISH] MQTT reconnected successfully");
        } else {
            return;
        }
    }
    
    time_t currentEpoch = mqttManager.getCurrentTime();
    if (currentEpoch == 0) {
        currentEpoch = millis() / 1000;
    }
    
    static StaticJsonDocument<1024> telemetryDoc;
    telemetryDoc.clear();
    
    telemetryDoc["device_id"] = THING_NAME;
    telemetryDoc["timestamp"] = String(currentEpoch);
    telemetryDoc["Temperature"] = String(measurements.temperature, 3);
    telemetryDoc["relay_state"] = relayState;
    telemetryDoc["firmware_version"] = loadFirmwareVersion();
    
    JsonObject voltage = telemetryDoc.createNestedObject("voltage");
    voltage["rms_v"] = String(roundf(measurements.voltage_rms * 1000) / 1000.0, 3);
    
    JsonObject current = telemetryDoc.createNestedObject("current");
    current["rms_a"] = String(roundf(measurements.current_rms * 1000) / 1000.0, 3);
    
    JsonObject power = telemetryDoc.createNestedObject("power");
    power["active_w"] = String(roundf(measurements.active_power * 1000) / 1000.0, 3);
    power["reactive_var"] = String(roundf(measurements.reactive_power * 1000) / 1000.0, 3);
    power["apparent_va"] = String(roundf(measurements.apparent_power * 1000) / 1000.0, 3);
    
    JsonObject energy = telemetryDoc.createNestedObject("energy");
    energy["cumulative_wh"] = String(roundf(cumulativeEnergy * 1000) / 1000.0, 3);
    
    JsonObject power_quality = telemetryDoc.createNestedObject("power_quality");
    power_quality["power_factor"] = String(roundf(measurements.power_factor * 1000) / 1000.0, 3);
    power_quality["frequency_hz"] = String(roundf(measurements.frequency * 1000) / 1000.0, 3);
    power_quality["phase_angle_deg"] = "0.000";
    
    JsonObject wifi_info = telemetryDoc.createNestedObject("wifi");
    wifi_info["rssi_dbm"] = wifiManager.getRSSI();
    wifi_info["ip_address"] = wifiManager.getIPAddress();
    
    extern char wifi_ssid[32];
    wifi_info["ssid"] = wifi_ssid;
    
    // ===== Add OTA info to telemetry =====
    JsonObject ota_info = telemetryDoc.createNestedObject("ota");
    ota_info["status"] = getOTAStatusString(getOTAStatus());
    ota_info["version"] = loadFirmwareVersion();
    ota_info["partition"] = getRunningPartitionInfo();
    
    static char telemetryBuffer[1024];
    serializeJson(telemetryDoc, telemetryBuffer, sizeof(telemetryBuffer));
    
    bool telemetryPublished = mqttManager.publishTelemetry(telemetryBuffer);
    
    if (telemetryPublished) {
        Serial.println("[PUBLISH] Telemetry published with OTA info");
    }
    
    bool shadowUpdated = mqttManager.updateShadow(
        measurements.voltage_rms,
        measurements.current_rms,
        measurements.active_power,
        cumulativeEnergy,
        measurements.temperature,
        relayState
    );
    
    if (shadowUpdated) {
        Serial.println("[PUBLISH] Shadow updated");
    }
}

void updateShadow() {
    if (!mqttManager.isConnected() || isOTARunning) {
        return;
    }
    
    mqttManager.updateShadow(
        measurements.voltage_rms,
        measurements.current_rms,
        measurements.active_power,
        cumulativeEnergy,
        measurements.temperature,
        relayState
    );
}

void controlRelay(bool state) {
    if (state != relayState) {
        relayState = state;
        digitalWrite(RELAY_PIN, state ? HIGH : LOW);
        Serial.printf("Relay turned %s\n", state ? "ON" : "OFF");
        
        saveEnergyToNVS();
        
        if (wifiManager.isConnected() && mqttManager.isConnected() && adeInitialized && !isOTARunning) {
            updateShadow();
        }
    }
}

void toggleRelay() {
    controlRelay(!relayState);
}