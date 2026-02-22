#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "ArduinoNvs.h"

// WiFi credentials (declared in cpp)
extern char wifi_ssid[32];
extern char wifi_password[64];

// Configuration constants
#define WIFI_TIMEOUT 30000
#define CAPTIVE_PORTAL_TIMEOUT 300000
#define LED_BLINK_INTERVAL 500
#define CAPTIVE_PORTAL_SSID_PREFIX "SmartPlug_Setup_"
#define CAPTIVE_PORTAL_PASSWORD ""
#define DNS_PORT 53
#define NVS_NAMESPACE "wifi_config"
#define RESET_BUTTON_HOLD_TIME 10000
#define RESET_CONFIRM_WINDOW 5000
#define RESET_BLINK_SLOW_INTERVAL 500
#define RESET_BLINK_FAST_INTERVAL 200
#define RESET_BLINK_RAPID_INTERVAL 100

class WiFiManager {
public:
    WiFiManager();
    
    // Main methods
    bool begin();
    void handleClient();
    void reconnect();
    
    // Status methods
    bool isConnected();
    bool isInSetupMode();
    String getIPAddress();
    int getRSSI();
    
    // Credential management
    bool saveCredentials(const char* ssid, const char* password);
    bool resetCredentials();
    bool loadCredentials();
    
    // LED control
    void setLedCallback(void (*callback)(bool state));
    void blinkLed(bool enable);
    
    // Captive portal control
    void startCaptivePortal();
    void stopCaptivePortal();
    
    // Debug methods
    void printCredentials();
    
    // Button reset functionality
    void handleResetButton(bool buttonPressed, unsigned long buttonPressDuration);
    bool isResetting() { return resetInProgress; }
    void updateResetIndicator();
    
private:
    // WiFi management
    void generateSetupNetworkName();
    
    // Web handlers
    void handleRoot();
    void handleWifiSettings();
    void handleScanNetworks();
    void handleConnect();
    void handleReset();
    void handleStatus();
    void handleNotFound();
    
    // HTML utilities
    String getHtmlTemplate(const String& title, const String& content);
    String escapeHtml(const String& input);
    
    // Member variables
    unsigned long lastReconnectAttempt;
    unsigned long reconnectInterval;
    bool setupMode;
    
    // LED control
    bool ledBlinking;
    unsigned long lastBlinkTime;
    void (*ledCallback)(bool state);
    
    // Captive portal
    unsigned long portalStartTime;
    WebServer* webServer;
    DNSServer* dnsServer;
    String setupNetworkName;
    
    // Reset functionality
    bool resetInProgress;
    unsigned long resetStartTime;
    unsigned long resetBlinkStartTime;
    int resetBlinkStage;
    unsigned long resetConfirmStartTime;
};

#endif