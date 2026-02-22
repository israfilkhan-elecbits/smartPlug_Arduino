#include "ota_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/md.h>
#include "ArduinoNvs.h"

#define OTA_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define OTA_DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define OTA_HTTP_TIMEOUT 15000
#define OTA_DOWNLOAD_BUFFER_SIZE 4096
#define OTA_NVS_NAMESPACE "ota_storage"
#define OTA_MAX_ATTEMPTS 3

volatile bool isOTARunning = false;      // Overall OTA in progress
volatile bool isOTADownloading = false;  // Download in progress
volatile bool isOTAUpdating = false;     // Flash write in progress
static OTAStatus currentOTAStatus = OTA_IDLE;

// ============ HELPER FUNCTIONS ============

bool hexToBytes(const char* hex, uint8_t* bytes, size_t length) {
    if (strlen(hex) != length * 2) return false;
    for (size_t i = 0; i < length; i++) {
        char byteStr[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        bytes[i] = (uint8_t)strtol(byteStr, NULL, 16);
    }
    return true;
}

void bytesToHex(const uint8_t* bytes, size_t length, char* output) {
    for (size_t i = 0; i < length; i++) {
        sprintf(output + (i * 2), "%02x", bytes[i]);
    }
    output[length * 2] = '\0';
}

// ============ MANIFEST PARSING ============

bool parseManifest(const String& json, OTAManifest& manifest) {
    OTA_DEBUG_PRINTLN("[OTA] Parsing manifest...");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        OTA_DEBUG_PRINTF("[OTA] JSON parse error: %s\n", error.c_str());
        manifest.valid = false;
        return false;
    }
    
    manifest.latest_version = doc["latest_version"] | "";
    manifest.firmware_url = doc["firmware_url"] | "";
    manifest.size = doc["size"] | 0;
    manifest.sha256 = doc["sha256"] | "";
    
    if (manifest.latest_version.length() == 0 ||
        manifest.firmware_url.length() == 0 ||
        manifest.size == 0 ||
        manifest.sha256.length() != 64) {
        OTA_DEBUG_PRINTLN("[OTA] Invalid manifest: missing or invalid fields");
        manifest.valid = false;
        return false;
    }
    
    manifest.valid = true;
    OTA_DEBUG_PRINTF("[OTA] Version: %s, Size: %d bytes\n",
                    manifest.latest_version.c_str(), manifest.size);
    return true;
}

// ============ VERSION COMPARISON ============

int compareSemanticVersions(const String& v1, const String& v2) {
    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    
    sscanf(v1.c_str(), "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2.c_str(), "%d.%d.%d", &major2, &minor2, &patch2);
    
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    if (patch1 < patch2) return -1;
    if (patch1 > patch2) return 1;
    return 0;
}

bool isUpdateAvailable(const String& current, const String& latest) {
    return compareSemanticVersions(current, latest) == -1;
}

// ============ OTA STATUS MANAGEMENT ============

void setOTAStatus(OTAStatus status) {
    currentOTAStatus = status;
    OTA_DEBUG_PRINTF("[OTA] Status: %s\n", getOTAStatusString(status).c_str());
}

OTAStatus getOTAStatus() {
    return currentOTAStatus;
}

String getOTAStatusString(OTAStatus status) {
    switch (status) {
        case OTA_IDLE:          return "Idle";
        case OTA_CHECKING:      return "Checking";
        case OTA_DOWNLOADING:   return "Downloading";
        case OTA_INSTALLING:    return "Installing";
        case OTA_SUCCESS:       return "Success";
        case OTA_FAILED:        return "Failed";
        case OTA_ROLLBACK:      return "Rollback";
        default:                return "Unknown";
    }
}

// ============ NVS STORAGE USING ARDUINONVS ============

bool saveFirmwareVersion(const String& version) {
    if (!NVS.begin(OTA_NVS_NAMESPACE)) {
        OTA_DEBUG_PRINTLN("[OTA] Failed to open NVS for version save");
        return false;
    }
    
    bool success = NVS.setString("fw_version", version);
    if (success) {
        success = NVS.commit();
    }
    
    NVS.close();
    
    if (success) {
        OTA_DEBUG_PRINTF("[OTA] Saved version: %s\n", version.c_str());
    } else {
        OTA_DEBUG_PRINTLN("[OTA] Failed to save version");
    }
    
    return success;
}

String loadFirmwareVersion() {
    if (!NVS.begin(OTA_NVS_NAMESPACE)) {
        OTA_DEBUG_PRINTLN("[OTA] Failed to open NVS for version load");
        return "1.0.0";
    }
    
    String version = NVS.getString("fw_version");
    if (version.length() == 0) {
        version = "1.0.0";
        // Try to save default version
        NVS.setString("fw_version", version);
        NVS.commit();
    }
    
    NVS.close();
    OTA_DEBUG_PRINTF("[OTA] Loaded version: %s\n", version.c_str());
    return version;
}

bool saveOTAAttempt(uint8_t attempt) {
    if (!NVS.begin(OTA_NVS_NAMESPACE)) {
        OTA_DEBUG_PRINTLN("[OTA] Failed to open NVS for attempt save");
        return false;
    }
    
    bool success = NVS.setInt("ota_attempts", attempt);
    if (success) {
        success = NVS.commit();
    }
    
    NVS.close();
    
    if (success) {
        OTA_DEBUG_PRINTF("[OTA] Saved attempt: %d\n", attempt);
    }
    
    return success;
}

uint8_t loadOTAAttempt() {
    if (!NVS.begin(OTA_NVS_NAMESPACE)) {
        OTA_DEBUG_PRINTLN("[OTA] Failed to open NVS for attempt load");
        return 0;
    }
    
    // ArduinoNvs doesn't have hasKey(), so we try to get the value
    // and check if it's the default
    int attempts = NVS.getInt("ota_attempts");
    
    // If the key doesn't exist, getInt returns 0 by default
    // So we just return whatever we got (0 is fine for no attempts)
    
    NVS.close();
    
    OTA_DEBUG_PRINTF("[OTA] Loaded attempts: %d\n", attempts);
    return (uint8_t)attempts;
}

bool clearOTAAttempts() {
    if (!NVS.begin(OTA_NVS_NAMESPACE)) {
        OTA_DEBUG_PRINTLN("[OTA] Failed to open NVS for clearing attempts");
        return false;
    }
    
    bool success = NVS.erase("ota_attempts");
    if (success) {
        success = NVS.commit();
    }
    
    NVS.close();
    
    if (success) {
        OTA_DEBUG_PRINTLN("[OTA] Cleared OTA attempts");
    }
    
    return success;
}

// ============ PARTITION INFO ============

String getRunningPartitionInfo() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return "Unknown";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (0x%x)", running->label, running->address);
    return String(buf);
}

String getNextPartitionInfo() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (!next) return "Unknown";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (0x%x)", next->label, next->address);
    return String(buf);
}

size_t getFreeHeap() {
    return ESP.getFreeHeap();
}

// ============ ROLLBACK SUPPORT ============

bool markAppValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            OTA_DEBUG_PRINTLN("[OTA] Marking app as valid - cancelling rollback");
            esp_ota_mark_app_valid_cancel_rollback();
            clearOTAAttempts();
            return true;
        }
    }
    return false;
}

bool isRollbackBoot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        return (ota_state == ESP_OTA_IMG_ABORTED || 
                ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    }
    return false;
}

bool isBootedFromNewPartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    return (running == next);
}

// ============ DOWNLOAD AND UPDATE ============

bool downloadAndUpdate(const OTAManifest& manifest, OTAProgressCallback progressCb) {
    // Check if OTA is already running
    if (isOTARunning) {
        OTA_DEBUG_PRINTLN("[OTA] ERROR: OTA already in progress!");
        setOTAStatus(OTA_FAILED);
        return false;
    }
    
    if (!manifest.valid) {
        setOTAStatus(OTA_FAILED);
        return false;
    }
    
    // Set OTA running flags
    isOTARunning = true;
    isOTADownloading = true;
    isOTAUpdating = false;
    
    OTA_DEBUG_PRINTLN("[OTA] Starting download...");
    setOTAStatus(OTA_DOWNLOADING);
    
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < (OTA_DOWNLOAD_BUFFER_SIZE + 8192)) {
        OTA_DEBUG_PRINTF("[OTA] Insufficient heap! Free: %d, Need: %d\n", 
                        freeHeap, OTA_DOWNLOAD_BUFFER_SIZE + 8192);
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTADownloading = false;
        return false;
    }
    
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    
    HTTPClient http;
    http.begin(secureClient, manifest.firmware_url);
    http.setTimeout(OTA_HTTP_TIMEOUT);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        OTA_DEBUG_PRINTF("[OTA] HTTP failed: %d\n", httpCode);
        http.end();
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTADownloading = false;
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0 || (size_t)contentLength != manifest.size) {
        OTA_DEBUG_PRINTF("[OTA] Size mismatch: %d vs %d\n", contentLength, manifest.size);
        http.end();
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTADownloading = false;
        return false;
    }
    
    uint8_t attempts = loadOTAAttempt() + 1;
    saveOTAAttempt(attempts);
    OTA_DEBUG_PRINTF("[OTA] Update attempt #%d\n", attempts);
    
    if (!Update.begin(manifest.size, U_FLASH)) {
        OTA_DEBUG_PRINTF("[OTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTADownloading = false;
        return false;
    }
    
    OTA_DEBUG_PRINTLN("[OTA] Installing firmware...");
    setOTAStatus(OTA_INSTALLING);
    isOTADownloading = false;
    isOTAUpdating = true;
    
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[OTA_DOWNLOAD_BUFFER_SIZE];
    size_t bytesWritten = 0;
    unsigned long lastProgress = 0;
    unsigned long lastWDTFeed = 0;
    
    while (http.connected() && bytesWritten < manifest.size) {
        // Check if OTA flag is still set (should be, but just in case)
        if (!isOTARunning) {
            OTA_DEBUG_PRINTLN("[OTA] OTA aborted by external flag!");
            Update.abort();
            mbedtls_md_free(&ctx);
            http.end();
            setOTAStatus(OTA_FAILED);
            isOTAUpdating = false;
            return false;
        }
        
        size_t available = stream->available();
        if (available) {
            size_t toRead = min(available, sizeof(buffer));
            size_t read = stream->readBytes(buffer, toRead);
            
            if (read > 0) {
                mbedtls_md_update(&ctx, buffer, read);
                if (Update.write(buffer, read) != read) {
                    OTA_DEBUG_PRINTLN("[OTA] Write failed!");
                    Update.abort();
                    mbedtls_md_free(&ctx);
                    http.end();
                    setOTAStatus(OTA_FAILED);
                    isOTARunning = false;
                    isOTAUpdating = false;
                    return false;
                }
                bytesWritten += read;
                
                if (progressCb) {
                    unsigned long now = millis();
                    if (now - lastProgress > 1000) {
                        lastProgress = now;
                        int percent = (bytesWritten * 100) / manifest.size;
                        progressCb(percent, bytesWritten, manifest.size);
                    }
                }
                
                if (millis() - lastWDTFeed > 3000) {
                    esp_task_wdt_reset();
                    lastWDTFeed = millis();
                }
            }
        }
        delay(1);
    }
    
    uint8_t calculatedHash[32];
    mbedtls_md_finish(&ctx, calculatedHash);
    mbedtls_md_free(&ctx);
    http.end();
    
    if (bytesWritten != manifest.size) {
        OTA_DEBUG_PRINTLN("[OTA] Incomplete download");
        Update.abort();
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTAUpdating = false;
        return false;
    }
    
    char calcHex[65];
    bytesToHex(calculatedHash, 32, calcHex);
    if (!manifest.sha256.equalsIgnoreCase(calcHex)) {
        OTA_DEBUG_PRINTLN("[OTA] SHA256 Mismatch!");
        OTA_DEBUG_PRINTF("  Expected: %s\n", manifest.sha256.c_str());
        OTA_DEBUG_PRINTF("  Calculated: %s\n", calcHex);
        Update.abort();
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTAUpdating = false;
        return false;
    }
    
    OTA_DEBUG_PRINTLN("[OTA] SHA256 verification successful");
    
    if (!Update.end(true)) {
        OTA_DEBUG_PRINTF("[OTA] Update.end failed: %s\n", Update.errorString());
        setOTAStatus(OTA_FAILED);
        isOTARunning = false;
        isOTAUpdating = false;
        return false;
    }
    
    OTA_DEBUG_PRINTLN("[OTA] Update successful! Restarting...");
    setOTAStatus(OTA_SUCCESS);
    
    saveFirmwareVersion(manifest.latest_version);
    clearOTAAttempts();
    
    // Keep flags set until restart
    isOTAUpdating = false;
    isOTADownloading = false;
    
    delay(1000);
    ESP.restart();
    return true;
}