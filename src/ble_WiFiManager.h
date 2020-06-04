/**
 * Abstract away configuration utility of WiFi credentials over Bluetooth-LE.
 * 
 * Based on Bernd Giescke's (beegee1962) sketch for WiFi configuration over BLE:
 * Documentation: https://desire.giesecke.tk/index.php/2018/04/06/esp32-wifi-setup-over-ble/
 * Code: https://bitbucket.org/beegee1962/esp32_wifi_ble_esp32/src/master/
 * 
 * Additions made by Uri Shani (UriShX), 02/2020: 
 * 1. Additional characteristic for getting SSID list over BLE (read only)
 * 2. Additional characteristic for serving connection status as notifications, every 1 second
 * 
 * Published under the MIT license, see LICENSE.md
 */

#pragma once

#include "Arduino.h"
// Headers for ESP32 BLE
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
// BLE notify and indicate properties, used for connection status update
#include <BLE2902.h>
// Includes for JSON object handling
// Requires ArduinoJson library, ver.<6 (latest checked: 5.13.4)
// https://arduinojson.org
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>
// Flash storage of variables (instead of EEPROM)
#include <Preferences.h>
// Non volatile storage controller
#include <nvs.h>
#include <nvs_flash.h>
// WiFi includes
#include <WiFi.h>
// #include <WiFiMulti.h>

// List of Service and Characteristic UUIDs
// service & wifi uuid are what's used in the original sketch, to maintain compatibility
// list & status uuid were randomly generated using https://www.uuidgenerator.net/
#define DEF_SERVICE_UUID "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define DEF_WIFI_UUID "00005555-ead2-11e7-80c1-9a214cf093ae"
#define DEF_WIFI_LIST_UUID "1d338124-7ddc-449e-afc7-67f8673a1160"
#define DEF_WIFI_STATUS_UUID "5b3595c4-ad4f-4e1e-954e-3b290cc02eb0"

// #ifdef WiFiManager_h
// #define MANAGE_WIFI true
// #elif
// #define MANAGE_WIFI false
// #endif

#if ARDUINOJSON_VERSION_MAJOR != 5
#error ArduinoJson v. 5 is required
#endif

#define BLEWIFI_NAMESPACE BleWifiConfig
#define BEGIN_BLE_WIFI_CONFIG_NAMESPACE \
    namespace BLE_WIFI_CONFIG_NAMESPACE \
    {
#define END_BLE_WIFI_CONFIG_NAMESPACE }

#define USING_NAMESPACE_BLE_WIFI_CONFIG using namespace BLE_WIFI_CONFIG_NAMESPACE;

BEGIN_BLE_WIFI_CONFIG_NAMESPACE

/*! \brief Create an instance of the library
 */
#define BLE_WIFI_CONFIG_CREATE_INSTANCE(Name) \
    BLE_WIFI_CONFIG_NAMESPACE::BleWifiConfigInterface Name;

class BleWifiConfigCommonInterface
{
protected:
public:
    BleWifiConfigCommonInterface() {}

protected:
};

class BleWifiConfigInterface : public BleWifiConfigCommonInterface
{
protected:
    // ESP32
    /** BLE Service */
    BLEService *pService;
    /** BLE Server */
    BLEServer *pServer;

    /** Private UUIDs */
    std::string _sreviceUuid = DEF_SERVICE_UUID;
    std::string _wifiUuid = DEF_WIFI_UUID;
    std::string _listUuid = DEF_WIFI_LIST_UUID;
    std::string _statusUuid = DEF_WIFI_STATUS_UUID;

    bool _connected;

public:
    // callbacks
    void (*_connectedCallback)() = NULL;
    void (*_disconnectedCallback)() = NULL;

    /** freeRTOS task handle */
    TaskHandle_t sendBLEdataTask;
    /** freeRTOS mutex handle */
    SemaphoreHandle_t connStatSemaphore;
    // public characteristics
    /** Characteristic for digital output */
    BLECharacteristic *pCharacteristicWiFi;
    /** Characteristic for found WiFi list */
    BLECharacteristic *pCharacteristicList;
    /** Characteristic for connection status */
    BLECharacteristic *pCharacteristicStatus;
    /** BLE Advertiser */
    BLEAdvertising *pAdvertising;

    // public vars
    /** Selected network 
		true = use primary network
		false = use secondary network
	*/
    bool usePrimAP = true;
    /** Flag if stored AP credentials are available */
    bool hasCredentials = false;
    /** Number of found SSIDs */
    int apNum = 0;
    /** Time of last SSID scan */
    unsigned long apScanTime;
    /** Connection status */
    volatile bool isConnected = false;
    /** Connection change status */
    bool connStatusChanged = false;
    /** BLE connection status */
    volatile bool deviceConnected = false;
    /** int representation of connected to primary ssid (1), secondary (2), or disconnected (0) */
    uint16_t sendVal = 0x0000;
    /** WiFi authentication mode types for enum parsing, based on esp_wifi_types.h */
    const String authModes[7] = {"open", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "WPA2_ENTERPRISE", "MAX"};
    /** WiFi connection status types for enum parsing from WiFiType.h */
    const String wifiConnStat[7] = {"WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "WL_SCAN_COMPLETED", "WL_CONNECTED", "WL_CONNECT_FAILED", "WL_CONNECTION_LOST", "WL_DISCONNECTED"};

    /** Preferences */
    /** SSIDs of local WiFi networks */
    String ssidPrim;
    String ssidSec;
    /** Password for local WiFi network */
    String pwPrim;
    String pwSec;

    /** Unique device name */
    String apName = "ESP32-xxxxxxxxxxxx";

protected:
    /**
	 * Create unique device name from MAC address
	 **/
    String createName()
    {
        // uint8_t baseMac[6];
        // // Get MAC address for WiFi station
        // esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
        // // Write unique name into apName
        // sprintf(apName, "ESP32-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);

        String hostString = String((uint32_t)ESP.getEfuseMac(), HEX);
        hostString.toUpperCase();
        return "ESP32-" + hostString;
    }

    /** WiFi SSIDs scan 
	 * Separated from ScanWiFi(), so could be used independetley
	 * @return int - number of found access points
	 */
    int actualWiFiScan()
    {
        Serial.println("Start scanning for networks");

        WiFi.disconnect(true);
        WiFi.enableSTA(true);
        WiFi.mode(WIFI_STA);

        // Scan for AP
        apScanTime = millis();
        int _apNum = WiFi.scanNetworks(false, true, false, 1000);
        if (_apNum == 0)
        {
            Serial.println("Found no networks?????");
            return false;
        }

        return _apNum;
    }

    void _init(std::string _sreviceUuid, std::string _wifiUuid, std::string _listUuid, std::string _statusUuid);

    // TODO why must these functions be inline??
    inline bool _begin(const char *deviceName);

public:
    BleWifiConfigInterface() {}

    ~BleWifiConfigInterface() {}

    void init(String sreviceUuid, String wifiUuid, String listUuid, String statusUuid)
    {
        _sreviceUuid = sreviceUuid.c_str();
        _wifiUuid = wifiUuid.c_str();
        _listUuid = listUuid.c_str();
        _statusUuid = statusUuid.c_str();

        _init(_sreviceUuid, _wifiUuid, _listUuid, _statusUuid);
    }

    void init(std::string sreviceUuid, std::string wifiUuid, std::string listUuid, std::string statusUuid)
    {
        _sreviceUuid = sreviceUuid;
        _wifiUuid = wifiUuid;
        _listUuid = listUuid;
        _statusUuid = statusUuid;

        _init(_sreviceUuid, _wifiUuid, _listUuid, _statusUuid);
    }

    void init(String sreviceUuid, String wifiUuid)
    {
        _sreviceUuid = sreviceUuid.c_str();
        _wifiUuid = wifiUuid.c_str();
        _listUuid = DEF_WIFI_LIST_UUID;
        _statusUuid = DEF_WIFI_STATUS_UUID;

        _init(_sreviceUuid, _wifiUuid, _listUuid, _statusUuid);
    }

    void init(std::string sreviceUuid, std::string wifiUuid)
    {
        _sreviceUuid = sreviceUuid;
        _wifiUuid = wifiUuid;
        _listUuid = DEF_WIFI_LIST_UUID;
        _statusUuid = DEF_WIFI_STATUS_UUID;

        _init(_sreviceUuid, _wifiUuid, _listUuid, _statusUuid);
    }

    void init()
    {
        _sreviceUuid = DEF_SERVICE_UUID;
        _wifiUuid = DEF_WIFI_UUID;
        _listUuid = DEF_WIFI_LIST_UUID;
        _statusUuid = DEF_WIFI_STATUS_UUID;

        _init(_sreviceUuid, _wifiUuid, _listUuid, _statusUuid);
    }

    // TODO why must these functions be inline??
    inline bool begin()
    {
        return _begin(apName.c_str());
    }

    bool startWiFiConnection();

    bool connectWiFi();

    int publicWifiScan()
    {
        apNum = actualWiFiScan();
        return apNum;
    }

    bool scanWiFi();
};

/**
 * MyServerCallbacks
 * Callbacks for client connection and disconnection
 */
class MyServerCallbacks : public BLEServerCallbacks
{
public:
    MyServerCallbacks(BleWifiConfigInterface *bleWifiConfigInterface)
    {
        _bleWifiConfigInterface = bleWifiConfigInterface;
    }

protected:
    BleWifiConfigInterface *_bleWifiConfigInterface;
    // TODO this doesn't take into account several clients being connected
    void onConnect(BLEServer *pServer)
    {
        Serial.println("BLE client connected");
        _bleWifiConfigInterface->deviceConnected = true;
        if (_bleWifiConfigInterface->_connectedCallback)
            _bleWifiConfigInterface->_connectedCallback();
    };

    void onDisconnect(BLEServer *pServer)
    {
        Serial.println("BLE client disconnected");
        _bleWifiConfigInterface->deviceConnected = false;
        _bleWifiConfigInterface->pAdvertising->start();
        if (_bleWifiConfigInterface->_disconnectedCallback)
            _bleWifiConfigInterface->_disconnectedCallback();
    }
};

/**
 * MyCallbackHandler
 * Callbacks for BLE client read/write requests
 */
class MyCallbackHandler : public BLECharacteristicCallbacks
{
public:
    MyCallbackHandler(BleWifiConfigInterface *bleWifiConfigInterface)
    {
        _bleWifiConfigInterface = bleWifiConfigInterface;
    }

protected:
    BleWifiConfigInterface *_bleWifiConfigInterface;

    /** Buffer for JSON string */
    // MAx size is 51 bytes for frame:
    // {"ssidPrim":"","pwPrim":"","ssidSec":"","pwSec":""}
    // + 4 x 32 bytes for 2 SSID's and 2 passwords
    StaticJsonBuffer<200> jsonBuffer;

    void onWrite(BLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();
        if (value.length() == 0)
        {
            return;
        }
        Serial.println("Received over BLE: " + String((char *)&value[0]));

        // Decode data
        int keyIndex = 0;
        for (int index = 0; index < value.length(); index++)
        {
            value[index] = (char)value[index] ^ (char)&_bleWifiConfigInterface->apName[keyIndex];
            keyIndex++;
            if (keyIndex >= _bleWifiConfigInterface->apName.length())
                keyIndex = 0;
        }

        /** Json object for incoming data */
        JsonObject &jsonIn = jsonBuffer.parseObject((char *)&value[0]);
        if (jsonIn.success())
        {
            if (jsonIn.containsKey("ssidPrim") &&
                jsonIn.containsKey("pwPrim") &&
                jsonIn.containsKey("ssidSec") &&
                jsonIn.containsKey("pwSec"))
            {
                _bleWifiConfigInterface->ssidPrim = jsonIn["ssidPrim"].as<String>();
                _bleWifiConfigInterface->pwPrim = jsonIn["pwPrim"].as<String>();
                _bleWifiConfigInterface->ssidSec = jsonIn["ssidSec"].as<String>();
                _bleWifiConfigInterface->pwSec = jsonIn["pwSec"].as<String>();

                Preferences BleWiFiPrefs;
                BleWiFiPrefs.begin("BleWiFiCred", false);
                BleWiFiPrefs.putString("ssidPrim", _bleWifiConfigInterface->ssidPrim);
                BleWiFiPrefs.putString("ssidSec", _bleWifiConfigInterface->ssidSec);
                BleWiFiPrefs.putString("pwPrim", _bleWifiConfigInterface->pwPrim);
                BleWiFiPrefs.putString("pwSec", _bleWifiConfigInterface->pwSec);
                BleWiFiPrefs.putBool("valid", true);
                BleWiFiPrefs.end();

                Serial.println("Received over bluetooth:");
                Serial.println("primary SSID: " + _bleWifiConfigInterface->ssidPrim + " password: " + _bleWifiConfigInterface->pwPrim);
                Serial.println("secondary SSID: " + _bleWifiConfigInterface->ssidSec + " password: " + _bleWifiConfigInterface->pwSec);
                _bleWifiConfigInterface->connStatusChanged = true;
                _bleWifiConfigInterface->hasCredentials = true;
            }
            else if (jsonIn.containsKey("erase"))
            {
                Serial.println("Received erase command");
                Preferences BleWiFiPrefs;
                BleWiFiPrefs.begin("BleWiFiCred", false);
                BleWiFiPrefs.clear();
                BleWiFiPrefs.end();
                _bleWifiConfigInterface->connStatusChanged = true;
                _bleWifiConfigInterface->hasCredentials = false;
                _bleWifiConfigInterface->ssidPrim = "";
                _bleWifiConfigInterface->pwPrim = "";
                _bleWifiConfigInterface->ssidSec = "";
                _bleWifiConfigInterface->pwSec = "";

                int err;
                err = nvs_flash_init();
                Serial.println("nvs_flash_init: " + err);
                err = nvs_flash_erase();
                Serial.println("nvs_flash_erase: " + err);
            }
            else if (jsonIn.containsKey("reset"))
            {
                WiFi.disconnect();
                esp_restart();
            }
        }
        else
        {
            Serial.println("Received invalid JSON");
        }
        jsonBuffer.clear();
    };

    void onRead(BLECharacteristic *pCharacteristic)
    {
        Serial.println("BLE onRead request");
        String wifiCredentials;

        /** Json object for outgoing data */
        JsonObject &jsonOut = jsonBuffer.createObject();
        jsonOut["ssidPrim"] = _bleWifiConfigInterface->ssidPrim;
        jsonOut["pwPrim"] = _bleWifiConfigInterface->pwPrim;
        jsonOut["ssidSec"] = _bleWifiConfigInterface->ssidSec;
        jsonOut["pwSec"] = _bleWifiConfigInterface->pwSec;
        // Convert JSON object into a string
        jsonOut.printTo(wifiCredentials);

        // encode the data
        int keyIndex = 0;
        Serial.println("Stored settings: " + wifiCredentials);
        for (int index = 0; index < wifiCredentials.length(); index++)
        {
            wifiCredentials[index] = (char)wifiCredentials[index] ^ (char)&_bleWifiConfigInterface->apName[keyIndex];
            keyIndex++;
            if (keyIndex >= _bleWifiConfigInterface->apName.length())
                keyIndex = 0;
        }
        pCharacteristic->setValue((uint8_t *)&wifiCredentials[0], wifiCredentials.length());
        jsonBuffer.clear();
    }
};

/** ListCallbackHandler
 * callback for SSID list read request
 */
class ListCallbackHandler : public BLECharacteristicCallbacks
{
public:
    ListCallbackHandler(BleWifiConfigInterface *bleWifiConfigInterface)
    {
        _bleWifiConfigInterface = bleWifiConfigInterface;
    }

protected:
    BleWifiConfigInterface *_bleWifiConfigInterface;

    // {"SSID":["","","","","","","","","",""]} + 10 x 32 bytes for 10 SSIDs, and some spare
    StaticJsonBuffer<500> ssidBuffer;

    void onRead(BLECharacteristic *pCharacteristic)
    {
        Serial.println("BLE onRead request");
        String wifiSSIDsFound;

        byte counter = 0;

        while (!&_bleWifiConfigInterface->apNum && counter < 20)
        {
            if (millis() - _bleWifiConfigInterface->apScanTime > 10000)
                _bleWifiConfigInterface->publicWifiScan();
            counter++;
            delay(500);
        }

        /** Json object for outgoing data */
        JsonObject &jsonOut = ssidBuffer.createObject();
        JsonArray &SSID = jsonOut.createNestedArray("SSID");
        for (int i = 0; i < (int)&_bleWifiConfigInterface->apNum && i < 10; i++)
        {
            String ssid = WiFi.SSID(i);

            if (WiFi.encryptionType(i) != 0)
            {
                SSID.add(ssid);
            }
        }
        // Convert JSON object into a string
        jsonOut.printTo(wifiSSIDsFound);

        // encode the data (doesn't seem necessary, if added should be added to web app as well)
        Serial.println("Found SSIDs: " + wifiSSIDsFound);
        // int keyIndex = 0;
        // for (int index = 0; index < wifiSSIDsFound.length(); index ++) {
        // 	wifiSSIDsFound[index] = (char) wifiSSIDsFound[index] ^ (char) apName[keyIndex];
        // 	keyIndex++;
        // 	if (keyIndex >= strlen(apName)) keyIndex = 0;
        // }
        pCharacteristic->setValue((uint8_t *)&wifiSSIDsFound[0], wifiSSIDsFound.length());
        ssidBuffer.clear();
    }
};

void BleWifiConfigInterface::_init(std::string _sreviceUuid, std::string _wifiUuid, std::string _listUuid, std::string _statusUuid)
{
    // Create unique device name
    apName = createName();

    if (_statusUuid != "")
    {
        // Set up mutex semaphore
        connStatSemaphore = xSemaphoreCreateMutex();

        if (connStatSemaphore == NULL)
        {
            Serial.println("Error creating connStatSemaphore");
        }

        // ble task
        xTaskCreate(
            sendBLEdata,
            "sendBLEdataTask",
            2048,
            NULL,
            1,
            &sendBLEdataTask);
        delay(500);
    }

    Preferences BleWiFiPrefs;
    BleWiFiPrefs.begin("BleWiFiCred", false);
    bool hasPref = BleWiFiPrefs.getBool("valid", false);
    if (hasPref)
    {
        ssidPrim = BleWiFiPrefs.getString("ssidPrim", "");
        ssidSec = BleWiFiPrefs.getString("ssidSec", "");
        pwPrim = BleWiFiPrefs.getString("pwPrim", "");
        pwSec = BleWiFiPrefs.getString("pwSec", "");

        Serial.printf("%s,%s,%s,%s\n", ssidPrim, pwPrim, ssidSec, pwSec);

        if (ssidPrim.equals("") || pwPrim.equals("") || ssidSec.equals("") || pwPrim.equals(""))
        {
            Serial.println("Found credentials but credentials are invalid");
        }
        else
        {
            Serial.println("Read from credentials:");
            Serial.println("primary SSID: " + ssidPrim + " password: " + pwPrim);
            Serial.println("secondary SSID: " + ssidSec + " password: " + pwSec);
            hasCredentials = true;
        }
    }
    else
    {
        Serial.println("Could not find credentials, need send data over BLE");
    }
    BleWiFiPrefs.end();
}

bool BleWifiConfigInterface::_begin(const char *deviceName)
{
    if (BLEDevice::getInitialized())
    {
        return false;
    }

    BLEDevice::init(deviceName);
    BLEDevice::setPower(ESP_PWR_LVL_P7);

    // Create BLE Server
    pServer = BLEDevice::createServer();

    // Set server callbacks
    pServer->setCallbacks(new MyServerCallbacks(this));

    // Create BLE Service
    pService = pServer->createService(BLEUUID(_sreviceUuid), 20);

    // Create BLE Characteristic for WiFi settings
    pCharacteristicWiFi = pService->createCharacteristic(
        BLEUUID(_wifiUuid),
        // WIFI_UUID,
        BLECharacteristic::PROPERTY_READ |
            BLECharacteristic::PROPERTY_WRITE);
    pCharacteristicWiFi->setCallbacks(new MyCallbackHandler(this));

    // Create BLE characteristic for found SSIDs
    pCharacteristicList = pService->createCharacteristic(
        BLEUUID(_listUuid),
        BLECharacteristic::PROPERTY_READ);
    pCharacteristicList->setCallbacks(new ListCallbackHandler(this));

    // Create BLE Characteristic for status notifications
    pCharacteristicStatus = pService->createCharacteristic(
        BLEUUID(_statusUuid),
        BLECharacteristic::PROPERTY_NOTIFY);
    // pCharacteristicStatus->setCallbacks(new MyCallbacks()); // If only notifications no need for callback?
    pCharacteristicStatus->addDescriptor(new BLE2902());

    // Start the service
    pService->start();

    // Start advertising
    pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(_sreviceUuid);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks(this));

    return true;
}

bool BleWifiConfigInterface::startWiFiConnection()
{
    if (hasCredentials)
    {
        apScanTime = millis();
        // Check for available AP's
        if (!scanWiFi())
        {
            Serial.println("Could not find any AP");

            return false;
        }
        else
        {
            // If AP was found, start connection
            return connectWiFi();
        }
    }
}

/** BLE notification task
 * works independently from loop(), in a separate freeRTOS task.
 * if the esp32 device (server) is connected to a client, update the client every 1 second
 * of the wifi connection status.
 * in order to not cause interference between the two tasks, a mutex semaphore is used by the
 * wifi connection callbacks which update the variable, loop(), and the notification task.
 */
void sendBLEdata(void *parameter)
{
    BleWifiConfigInterface *_bleWifiConfigInterface = (BleWifiConfigInterface *)parameter; //static_cast<BleWifiConfigInterface *>(parameter);

    TickType_t xLastWakeTime;
    TickType_t xPeriod = pdMS_TO_TICKS(1000);

    xLastWakeTime = xTaskGetTickCount();

    bool notificationFlag = false;

    while (1)
    {
        // if the device is connected via BLE try to send notifications
        if (_bleWifiConfigInterface->deviceConnected)
        {
            // Take mutex, set value, give mutex
            xSemaphoreTake(_bleWifiConfigInterface->connStatSemaphore, 0);
            _bleWifiConfigInterface->pCharacteristicStatus->setValue(_bleWifiConfigInterface->sendVal);
            xSemaphoreGive(_bleWifiConfigInterface->connStatSemaphore);

            // test if notifications are enabled by client
            byte testNotify = *_bleWifiConfigInterface->pCharacteristicStatus->getDescriptorByUUID((uint16_t)0x2902)->getValue();

            // if enabled, send value over BLE
            if (testNotify == 1)
            {
                _bleWifiConfigInterface->pCharacteristicStatus->notify(); // Send the value to the app!
                if (!notificationFlag)
                {
                    Serial.println("started notification service");
                    notificationFlag = true;
                }
            }
            else if (notificationFlag)
            {
                // else print failure message to serial monitor
                Serial.print("notify failed, value of 0x2902 descriptor:\t");
                Serial.println(testNotify, HEX); //*pCharacteristicMeas->getDescriptorByUUID((uint16_t)0x2902)->getValue(), HEX);
                notificationFlag = false;
            }
        }
        // sleep task for 1000 ms
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/** scanWiFi
 * Scans for available networks 
 * and decides if a switch between
 * allowed networks makes sense
 * @return <code>bool</code> True if at least one allowed network was found
*/
bool BleWifiConfigInterface::scanWiFi()
{
    /** RSSI for primary network */
    int8_t rssiPrim;
    /** RSSI for secondary network */
    int8_t rssiSec;
    /** Result of this function */
    bool result = false;

    apNum = actualWiFiScan();

    byte foundAP = 0;
    bool foundPrim = false;

    for (int index = 0; index < apNum; index++)
    {
        String ssid = WiFi.SSID(index);
        Serial.println("Found AP: " + ssid + " RSSI: " + WiFi.RSSI(index) + " Encrytion: " + authModes[WiFi.encryptionType(index)]);
        if (!strcmp((const char *)&ssid[0], (const char *)&ssidPrim[0]))
        {
            Serial.println("Found primary AP");
            foundAP++;
            foundPrim = true;
            rssiPrim = WiFi.RSSI(index);
        }
        if (!strcmp((const char *)&ssid[0], (const char *)&ssidSec[0]))
        {
            Serial.println("Found secondary AP");
            foundAP++;
            rssiSec = WiFi.RSSI(index);
        }
    }

    switch (foundAP)
    {
    case 0:
        result = false;
        break;
    case 1:
        if (foundPrim)
        {
            usePrimAP = true;
        }
        else
        {
            usePrimAP = false;
        }
        result = true;
        break;
    default:
        Serial.printf("RSSI Prim: %d Sec: %d\n", rssiPrim, rssiSec);
        if (rssiPrim > rssiSec)
        {
            usePrimAP = true; // RSSI of primary network is better
        }
        else
        {
            usePrimAP = false; // RSSI of secondary network is better
        }
        result = true;
        break;
    }
    return result;
}

/** Callback for receiving IP address from AP */
void gotIP(system_event_id_t event)
{
    BleWifiConfigInterface *_bleWifiConfigInterface;

    _bleWifiConfigInterface->isConnected = true;
    _bleWifiConfigInterface->connStatusChanged = true;
    /** Check if ip corresponds to 1st or 2nd configured SSID 
	 * takes semaphore, sets (uint16_t)sendVal, and gives semaphore
	*/
    String connectedSSID = WiFi.SSID();
    xSemaphoreTake(_bleWifiConfigInterface->connStatSemaphore, portMAX_DELAY);
    if (connectedSSID == _bleWifiConfigInterface->ssidPrim)
    {
        // Serial.println("connected to primary SSID");
        _bleWifiConfigInterface->sendVal = 0x0001;
    }
    else if (connectedSSID == _bleWifiConfigInterface->ssidSec)
    {
        _bleWifiConfigInterface->sendVal = 0x0002;
    }
    xSemaphoreGive(_bleWifiConfigInterface->connStatSemaphore);
}

/** Callback for connection loss */
void lostCon(system_event_id_t event)
{
    BleWifiConfigInterface *_bleWifiConfigInterface;

    _bleWifiConfigInterface->isConnected = false;
    _bleWifiConfigInterface->connStatusChanged = true;
    /** if disconnected, take semaphore, set (uint16_t)sendVal = 0, give semaphore */
    xSemaphoreTake(_bleWifiConfigInterface->connStatSemaphore, portMAX_DELAY);
    _bleWifiConfigInterface->sendVal = 0x0000;
    xSemaphoreGive(_bleWifiConfigInterface->connStatSemaphore);
}

/**
 * Start connection to AP
 */
bool BleWifiConfigInterface::connectWiFi()
{
    // Setup callback function for successful connection
    WiFi.onEvent(gotIP, SYSTEM_EVENT_STA_GOT_IP);
    // Setup callback function for lost connection
    WiFi.onEvent(lostCon, SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.disconnect(true);
    WiFi.enableSTA(true);
    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.print("Start connection to ");
    if (usePrimAP)
    {
        Serial.println(ssidPrim);
        byte connStat = WiFi.begin(ssidPrim.c_str(), pwPrim.c_str());
        if (connStat == 3)
            return true;
        else
        {
            Serial.printf("Connection failed: %s\n", wifiConnStat[connStat]);
            return false;
        }
    }
    else
    {
        Serial.println(ssidSec);
        byte connStat = WiFi.begin(ssidSec.c_str(), pwSec.c_str());
        if (connStat == 3)
            return true;
        else
        {
            Serial.printf("Connection failed: %s\n", wifiConnStat[connStat]);
            return false;
        }
    }
}

END_BLE_WIFI_CONFIG_NAMESPACE
