# Smart Plug 

An advanced IoT-enabled smart plug built on ESP32 with precise energy monitoring using the ADE9153A energy metering IC and seamless integration with AWS IoT Core.

## Features

### Core Functionality
- **Real-time Power Monitoring**: Voltage, current, active/reactive/apparent power, power factor, and frequency measurements
- **Energy Tracking**: Cumulative energy consumption in watt-hours with persistent storage
- **Remote Control**: Control relay state via AWS IoT Device Shadow or MQTT
- **AWS IoT Integration**: Secure MQTT communication with AWS IoT Core using X.509 certificates
- **WiFi Provisioning**: Captive portal setup mode for easy WiFi configuration
- **Offline Operation**: Local data storage when cloud connectivity is lost

### Advanced Features
- **Zero-Crossing Detection**: Synchronized measurements with AC waveform for improved accuracy
- **Temperature Monitoring**: Built-in temperature sensor readings
- **Overload Protection**: Configurable safety features
- **Device Shadow Sync**: Automatic state synchronization with AWS IoT Device Shadow
- **NVS Storage**: Non-volatile storage for WiFi credentials, energy data, and device state
- **Factory Reset**: 10-second button hold to reset device to factory defaults

> **Note**: OTA (Over-the-Air Updates) and LWT (Last Will and Testament) features are planned for future releases.

### Planned Features (Coming Soon)
- **OTA Support**: Over-the-air firmware updates capability
- **LWT (Last Will and Testament)**: MQTT connection status monitoring

## Hardware Requirements

### Components
- **ESP32 Development Board** (4MB Flash minimum)
- **ADE9153A Energy Metering IC** - High-precision single-phase energy measurement
- **Relay Module** - For load switching (connected to GPIO 33)
- **Push Button** - For manual control and factory reset (GPIO 23)
- **Status LED** - System status indicator (GPIO 22)

### Pin Configuration

#### SPI Interface (ADE9153A)
| Pin Function | GPIO Pin |
|--------------|----------|
| SCK          | GPIO 26  |
| MISO         | GPIO 25  |
| MOSI         | GPIO 27  |
| CS           | GPIO 12  |
| RESET        | GPIO 19  |

#### Control & Indicators
| Component       | GPIO Pin |
|-----------------|----------|
| Relay           | GPIO 33  |
| Status LED      | GPIO 22  |
| Button          | GPIO 23  |
| Zero-Crossing   | GPIO 21  |

## Getting Started

### Software Requirements

1. **PlatformIO IDE** or **PlatformIO Core**
   - Download from: https://platformio.org/install
   
2. **AWS IoT Core Account**
   - Create an AWS account if you don't have one
   - Access AWS IoT Core from AWS Console

### Installation Steps

#### Step 1: Configure AWS IoT

1. **Create a Thing in AWS IoT Core:**
   - Open AWS IoT Core Console
   - Navigate to **Manage** → **Things** → **Create**
   - Create a thing named `Smart_Plug_1`
   - Note: You can change this name, but update it in `MQTT_manager.h`

2. **Generate Certificates:**
   - Create and download the following:
     - Device certificate (certificate.pem.crt)
     - Private key (private.pem.key)
     - Amazon Root CA 1 (AmazonRootCA1.pem)
   - Keep these files secure

3. **Create and Attach Policy:**
   - Create a new policy with the following permissions:
   ```json
   {
     "Version": "2012-10-17",
     "Statement": [
       {
         "Effect": "Allow",
         "Action": [
           "iot:Connect",
           "iot:Publish",
           "iot:Subscribe",
           "iot:Receive"
         ],
         "Resource": "*"
       }
     ]
   }
   ```
   - Attach this policy to your certificate

4. **Get Your AWS IoT Endpoint:**
   - Go to **Settings** in AWS IoT Core Console
   - Copy your endpoint URL (e.g., `xxxxx-ats.iot.region.amazonaws.com`)

#### Step 2: Update Project Configuration

1. **Update AWS IoT Endpoint:**
   - Open `include/MQTT_manager.h`
   - Update the endpoint:
   ```cpp
   #define AWS_IOT_ENDPOINT "your-endpoint-ats.iot.ap-south-1.amazonaws.com"
   ```

2. **Update Thing Name (if changed):**
   ```cpp
   #define THING_NAME "Smart_Plug_1"
   ```

3. **Update Certificates:**
   - Open `src/MQTT_manager.cpp`
   - Replace the certificate contents:

   **AWS_CERT_CA** - Paste Amazon Root CA 1:
   ```cpp
   const char AWS_CERT_CA[] PROGMEM = R"EOF(
   -----BEGIN CERTIFICATE-----
   [Paste your Amazon Root CA 1 here]
   -----END CERTIFICATE-----
   )EOF";
   ```

   **AWS_CERT_CRT** - Paste Device Certificate:
   ```cpp
   const char AWS_CERT_CRT[] PROGMEM = R"EOF(
   -----BEGIN CERTIFICATE-----
   [Paste your device certificate here]
   -----END CERTIFICATE-----
   )EOF";
   ```

   **AWS_CERT_PRIVATE** - Paste Private Key:
   ```cpp
   const char AWS_CERT_PRIVATE[] PROGMEM = R"EOF(
   -----BEGIN RSA PRIVATE KEY-----
   [Paste your private key here]
   -----END RSA PRIVATE KEY-----
   )EOF";
   ```

4. **Update Timezone (Optional):**
   - Open `include/MQTT_manager.h`
   - Adjust GMT offset for your location:
   ```cpp
   #define GMT_OFFSET_SEC 19800  // +5:30 for India
   ```
   - Examples:
     - UTC+0: 0
     - UTC+5:30 (India): 19800
     - UTC-5 (EST): -18000
     - UTC+8 (China): 28800

#### Step 3: Build and Upload

1. **Open Project in PlatformIO:**
   - Open the project folder in VSCode with PlatformIO extension
   - Or use PlatformIO Core CLI

2. **Connect ESP32:**
   - Connect your ESP32 board via USB
   - Check the COM port in Device Manager (Windows) or `/dev/ttyUSB*` (Linux)

3. **Build the Project:**
   ```bash
   pio run
   ```

4. **Upload to ESP32:**
   ```bash
   pio run --target upload
   ```

5. **Monitor Serial Output:**
   ```bash
   pio run --target monitor
   ```
   - Or use serial monitor at 115200 baud

## WiFi Configuration

### First-Time Setup

1. **Power On Device:**
   - The device starts in Setup Mode (LED blinking)
   - Setup mode activates automatically if no WiFi credentials are saved

2. **Connect to Setup Network:**
   - On your phone/laptop, scan for WiFi networks
   - Connect to: `SmartPlug_Setup_XXXX`
   - (XXXX = last 4 digits of device MAC address)
   - No password required

3. **Configure WiFi:**
   - A captive portal should open automatically
   - If not, manually navigate to: `http://192.168.4.1`
   - Enter your WiFi SSID and password
   - Click **Connect**

4. **Device Connects:**
   - Device will restart
   - LED stops blinking when connected
   - Device connects to AWS IoT automatically

### Factory Reset Procedure

**To reset device to factory defaults:**

1. **Press and Hold Button:**
   - **0-5 seconds**: LED blinks slowly (500ms intervals)
   - **5-10 seconds**: LED blinks rapidly (100ms intervals)
   - Continue holding until 10 seconds

2. **Release Button:**
   - After 10 seconds, release the button

3. **Confirm Reset:**
   - Within 5 seconds, press the button again to confirm
   - If not confirmed, reset is cancelled

4. **Reset Complete:**
   - Device erases all stored WiFi Credentials
   - Restarts in Setup Mode

## MQTT Topics and Data Format

### Published Topics

**1. Telemetry Data** (1 Hz updates)
- **Topic**: `smartplug/telemetry`
- **Frequency**: Every 1 second

**2. Device Shadow Updates**
- **Topic**: `$aws/things/Smart_Plug_1/shadow/update`
- **Frequency**: On state change or every 1 second

### Subscribed Topics

**1. Control Commands**
- **Topic**: `smartplug/control`

**2. Shadow Delta**
- **Topic**: `$aws/things/Smart_Plug_1/shadow/update/delta`

### Telemetry Data Format

```json
{
  "device_id": "Smart_Plug_1",
  "timestamp": "1770708748",
  "Temperature": "31.211",
  "relay_state": true,
  "firmware_version": "1.2.0",
  "voltage": {
    "rms_v": "231.056"
  },
  "current": {
    "rms_a": "1.294"
  },
  "power": {
    "active_w": "311.824",
    "reactive_var": "nan",
    "apparent_va": "298.940"
  },
  "energy": {
    "cumulative_wh": "442.687"
  },
  "power_quality": {
    "power_factor": "1.000",
    "frequency_hz": "50.142",
    "phase_angle_deg": "0.000"
  },
  "wifi": {
    "rssi_dbm": -69,
    "ip_address": "192.168.1.181",
    "ssid": "Azoox 2.4GHz"
  }
}
```

### Control Commands

**Turn Relay ON/OFF:**
```json
{
  "relay_state": true
}
```

**Reset Energy Counter:**
```json
{
  "reset_energy": true
}
```

**Combined Commands:**
```json
{
  "relay_state": false,
  "reset_energy": true
}
```

## AWS IoT Device Shadow

### Shadow Document Structure

The device shadow maintains the desired and reported states:

```json
{
  "state": {
    "desired": {
      "welcome": "aws-iot",
      "relay_status": "true"
    },
    "reported": {
      "welcome": "aws-iot",
      "device_details": {
        "device_id": "Smart_Plug_1",
        "local_ip": "192.168.1.181",
        "wifi_ssid": "Azoox 2.4GHz"
      },
      "ota": {
        "fw_version": "1.2.0"
      },
      "device_diagnosis": {
        "network": "WiFi",
        "connection_attempt": "0",
        "timestamp": "1770708981",
        "last_reset": 670
      },
      "device_status": {
        "connected": "true",
        "rssi": "-50"
      },
      "meter_details": {
        "current_reading": "0.873",
        "power_reading": "211.016",
        "energy_total": "447.271",
        "voltage_reading": "231.628",
        "temperature": "31.211"
      },
      "relay_status": "true"
    }
  }
}
```

### Controlling via Shadow

To control the relay through AWS IoT Console:

1. Go to AWS IoT Core → Manage → Things → Smart_Plug_1
2. Click on "Device Shadow"
3. Click "Edit" on Classic Shadow
4. Update the desired state:
```json
{
  "state": {
    "desired": {
      "relay_status": "true"
    }
  }
}
```

## Configuration and Calibration

### Calibration Values

For accurate measurements, calibrate these coefficients based on your hardware:

**Location:** `src/main.cpp`

```cpp
struct CalibrationData {
    float voltage_coefficient = 13.0068f;   // Voltage calibration
    float current_coefficient = 0.36503161f;  // Current calibration **TODO
    float power_coefficient = 0.66498695f;    // Power calibration
    float energy_coefficient = 0.858307f;   // Energy calibration
    float current_offset = 0.019f;          // Current offset compensation
};
```

### Calibration Procedure

1. **Voltage Calibration:**
   - Connect a known accurate AC voltmeter
   - Compare readings
   - Adjust `voltage_coefficient`

2. **Current Calibration:**
   - Use a known resistive load
   - Measure current with calibrated ammeter
   - Adjust `current_coefficient`

3. **Power Calibration:**
   - Use resistive load (pure power factor)
   - Compare with power meter
   - Adjust `power_coefficient`

### Measurement Settings

**Location:** `src/main.cpp`

```cpp
// Measurement interval (milliseconds)
#define MEASUREMENT_INTERVAL_MS   100     // 10 Hz sampling rate

// Publishing interval (milliseconds)  
const unsigned long PUBLISH_INTERVAL = 1000;  // 1 Hz cloud updates

// Number of samples for averaging
#define DEFAULT_AVERAGE_SAMPLES 3
```

## Project Structure

```
Smart_Plug_With_Shadow_3.2.6_CF-2/
├── .pio/                    # PlatformIO build files (auto-generated)
├── .vscode/                 # VSCode configuration
├── include/                 # Header files
│   ├── ADE9153A.h          # ADE9153A register definition s
│   ├── ADE9153AAPI.h       # ADE9153A API interface
│   ├── ArduinoNvs.h        # Non-volatile storage library
│   ├── MQTT_manager.h      # AWS IoT MQTT client header
│   ├── wifi_manager.h      # WiFi & captive portal header
│   └── README              # Include folder documentation
├── lib/                     # External libraries (if needed)
├── src/                     # Source code
│   ├── ADE9153AAPI.cpp     # ADE9153A implementation
│   ├── ArduinoNvs.cpp      # NVS storage implementation
│   ├── main.cpp            # Main application logic
│   ├── MQTT_manager.cpp    # AWS IoT MQTT implementation
│   └── wifi_manager.cpp    # WiFi management implementation
├── test/                    # Unit tests (if any)
├── .gitignore              # Git ignore rules
├── partitions.csv          # ESP32 partition table
├── platformio.ini          # PlatformIO configuration
└── README.md               # Documentation
```

## Performance Specifications

### Measurement Accuracy
- **Voltage**: ±0.1% (after calibration)
- **Current**: ±0.2% (after calibration)
- **Power**: ±0.5% (after calibration)
- **Energy**: Cumulative, stored every 5 minutes

### System Performance
- **Sampling Rate**: 10 Hz (100ms interval)
- **Cloud Publishing**: 1 Hz (1 second interval)
- **WiFi Reconnection**: Automatic with 5-second retry
- **MQTT Keep-Alive**: 60 seconds
- **NVS Save Interval**: 5 minutes or on state change

### Network Performance
- **Connection Time**: ~5-10 seconds (typical)
- **MQTT Latency**: <500ms (typical)
- **Shadow Sync**: Real-time on state change

## Troubleshooting

### Device Won't Connect to WiFi

**Problem**: LED keeps blinking, won't connect

**Solutions:**
1. Verify WiFi credentials are correct
2. Check WiFi signal strength (should be > -70 dBm)
3. Ensure WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
4. Try factory reset and reconfigure
5. Check serial monitor for error messages

**Serial Monitor Commands:**
```bash
pio device monitor --baud 115200
```

### AWS IoT Connection Fails

**Problem**: WiFi connected but MQTT fails

**Solutions:**
1. **Verify Certificates:**
   - Ensure all three certificates are correctly pasted
   - Check for missing BEGIN/END lines
   - Ensure no extra spaces or line breaks

2. **Check AWS IoT Endpoint:**
   - Verify endpoint URL is correct
   - Ensure it ends with `.amazonaws.com`

3. **Time Synchronization:**
   - Device time MUST be correct for SSL
   - Check NTP server accessibility
   - Serial monitor shows: `[TIME] Time synced: ...`

4. **Verify Thing Name:**
   - Must match in code and AWS IoT Console
   - Check spelling and case

5. **Check Policies:**
   - Ensure policy allows connect, publish, subscribe
   - Verify policy is attached to certificate

6. **AWS IoT Core Quota:**
   - Check if you've exceeded free tier limits

### Inaccurate Measurements

**Problem**: Voltage/current readings are wrong

**Solutions:**
1. **Calibration Required:**
   - Use calibrated reference meter
   - Adjust coefficients in `main.cpp`

2. **Wiring Check:**
   - Verify all SPI connections to ADE9153A
   - Check ground connections
   - Ensure proper power supply

3. **Zero-Crossing Detection:**
   - Check GPIO 21 connection
   - Monitor serial for ZC errors

4. **ADC Clipping:**
   - Check `waveform_clipped` flag in telemetry
   - Reduce input if clipping occurs

### Device Resets Randomly

**Problem**: Unexpected reboots

**Solutions:**
1. **Power Supply:**
   - Use adequate 5V power supply (≥1A)
   - Check voltage stability

2. **Check Serial Monitor:**
   - Look for exception decoder output
   - Check for stack overflow errors

3. **Reduce Debug Level:**
   - Lower `CORE_DEBUG_LEVEL` in platformio.ini

### Offline Data Not Saving

**Problem**: Data lost when WiFi disconnects

**Solutions:**
1. **Check NVS:**
   - Monitor serial for NVS save messages
   - Ensure sufficient NVS partition space

2. **Storage Interval:**
   - Default is 5 minutes
   - Adjust `STORAGE_SAVE_INTERVAL` if needed

## Security Considerations

### Network Security
- **TLS 1.2 Encryption**: All MQTT communication encrypted
- **X.509 Certificates**: Mutual authentication with AWS IoT
- **Secure Storage**: WiFi credentials stored in encrypted NVS

### Best Practices
1. **Certificate Management:**
   - Never commit certificates to version control
   - Rotate certificates periodically
   - Deactivate compromised certificates immediately

2. **WiFi Security:**
   - Use WPA2/WPA3 encryption
   - Strong WiFi password
   - Disable WPS on router

3. **AWS IoT Policies:**
   - Use least privilege principle
   - Restrict topic access
   - Enable CloudWatch logging

4. **Physical Security:**
   - Secure device against tampering
   - Protect USB programming access

## Electrical Safety

### CRITICAL SAFETY WARNING

**This device controls AC mains voltage (110V-240V) which can be LETHAL.**

### Safety Requirements

1. **Qualified Personnel Only:**
   - Only qualified electricians should work with mains voltage
   - Follow local electrical codes and regulations

2. **Proper Enclosure:**
   - Use appropriately rated enclosure
   - Ensure proper grounding
   - Maintain required clearances

3. **Isolation:**
   - Use isolated power supply for ESP32
   - Proper isolation between high and low voltage
   - Never bypass safety features

4. **Testing:**
   - Test with low voltage first
   - Use isolated transformer during development
   - Have emergency shutoff accessible

5. **Certifications:**
   - For commercial use, get proper certifications
   - Follow UL/CE/FCC requirements
   - Liability insurance recommended

### Disclaimer

**Use at your own risk. The authors are not responsible for any damage, injury, or death resulting from the use of this project.**

## Support and Documentation

### Serial Monitor Output

Normal operation shows:
```
WiFi: Connected
MQTT: Connected
Relay: ON/OFF
ADE9153A: OK
RSSI: -45 dBm
IP: 192.168.1.100
```

### Debug Levels

Adjust in `platformio.ini`:
```ini
-DCORE_DEBUG_LEVEL=0  # None
-DCORE_DEBUG_LEVEL=1  # Error
-DCORE_DEBUG_LEVEL=2  # Warning
-DCORE_DEBUG_LEVEL=3  # Info (default)
-DCORE_DEBUG_LEVEL=4  # Debug
-DCORE_DEBUG_LEVEL=5  # Verbose
```

## Additional Resources

### Documentation Links
- **ESP32 Arduino Core**: https://docs.espressif.com/projects/arduino-esp32/
- **AWS IoT Core**: https://docs.aws.amazon.com/iot/
- **PlatformIO**: https://docs.platformio.org/
- **ADE9153A Datasheet**: Analog Devices website

### Libraries Used
- **PubSubClient**: MQTT client by Nick O'Leary
- **ArduinoJson**: JSON library by Benoît Blanchon
- **ArduinoNvs**: NVS wrapper for ESP32
- **ADE9153A**: Energy metering library

---

**Version**: 1.2.0  
**Last Updated**: February 2026  
**Firmware**: Smart_Plug_With_Shadow_3.2.6_CF-2