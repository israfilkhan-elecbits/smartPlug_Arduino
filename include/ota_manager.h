#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <functional>

// OTA Status Enum
enum OTAStatus {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_INSTALLING,
    OTA_SUCCESS,
    OTA_FAILED,
    OTA_ROLLBACK
};

// OTA Manifest Structure
struct OTAManifest {
    String latest_version;
    String firmware_url;
    size_t size;
    String sha256;
    bool valid;
    
    OTAManifest() : size(0), valid(false) {}
};

// OTA Callback Type
typedef std::function<void(int progress, size_t current, size_t total)> OTAProgressCallback;
typedef std::function<void(OTAStatus status, const String& message)> OTAStatusCallback;

// OTA Functions
bool parseManifest(const String& json, OTAManifest& manifest);
bool isUpdateAvailable(const String& current, const String& latest);
bool downloadAndUpdate(const OTAManifest& manifest, OTAProgressCallback progressCb = nullptr);
int compareSemanticVersions(const String& v1, const String& v2);

// OTA Status Functions
String getOTAStatusString(OTAStatus status);
void setOTAStatus(OTAStatus status);
OTAStatus getOTAStatus();

// Rollback & Info Functions
bool markAppValid();
bool isRollbackBoot();
bool isBootedFromNewPartition();
String getRunningPartitionInfo();
String getNextPartitionInfo();
size_t getFreeHeap();

// NVS Storage Functions
bool saveFirmwareVersion(const String& version);
String loadFirmwareVersion();
bool saveOTAAttempt(uint8_t attempt);
uint8_t loadOTAAttempt();
bool clearOTAAttempts();

// Global flags
extern volatile bool isOTARunning;      
extern volatile bool isOTADownloading;  
extern volatile bool isOTAUpdating;     

#endif