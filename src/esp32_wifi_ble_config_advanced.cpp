/**
 * WiFi configuration over BLE
 * 
 * Used to configure WiFi credentials over Bluetooth LE on a ESP32 WROOM.
 * 
 * Requirements:
 * ArduinoJson v.5 (checked with 5.13.4)
 * Confirmed working envioronments:
 * - Arduino 1.8.11 & esp32-arduino 1.0.4
 * - PlatformIO Home 3.1.0, Core 4.2.1, Espressif 32 1.11.2
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

// Default Arduino includes
#include <Arduino.h>
#include <WiFi.h>
// #include <WiFiMulti.h>
#include <nvs.h>
#include <nvs_flash.h>

// Includes for JSON object handling
// Requires ArduinoJson library, ver.<6 (latest checked: 5.13.4)
// https://arduinojson.org
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

// Includes for BLE
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
// BLE notify and indicate properties, used for connection status update
#include <BLE2902.h>

// Flash storage of variables (instead of EEPROM)
#include <Preferences.h>

// https://github.com/tzapu/WiFiManager
// Dev branch!!!!
#include <WiFiManager.h>

#include "ble_WiFiManager.h"

class Ble_WiFiManager
{
private:
	/** freeRTOS task handle */
	TaskHandle_t sendBLEdataTask;
	/** freeRTOS mutex handle */
	SemaphoreHandle_t connStatSemaphore;

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

		String hostString = String(WIFI_getChipId(), HEX);
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

	/** BLE notification task
	 * works independently from loop(), in a separate freeRTOS task.
	 * if the esp32 device (server) is connected to a client, update the client every 1 second
	 * of the wifi connection status.
	 * in order to not cause interference between the two tasks, a mutex semaphore is used by the
	 * wifi connection callbacks which update the variable, loop(), and the notification task.
	 */
	void sendBLEdata(void *parameter)
	{
		TickType_t xLastWakeTime;
		TickType_t xPeriod = pdMS_TO_TICKS(1000);

		xLastWakeTime = xTaskGetTickCount();

		bool notificationFlag = false;

		while (1)
		{
			// if the device is connected via BLE try to send notifications
			if (deviceConnected)
			{
				// Take mutex, set value, give mutex
				xSemaphoreTake(connStatSemaphore, 0);
				pCharacteristicStatus->setValue(sendVal);
				xSemaphoreGive(connStatSemaphore);

				// test if notifications are enabled by client
				byte testNotify = *pCharacteristicStatus->getDescriptorByUUID((uint16_t)0x2902)->getValue();

				// if enabled, send value over BLE
				if (testNotify == 1)
				{
					pCharacteristicStatus->notify(); // Send the value to the app!
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

public:
	/** Private UUIDs */
	std::string _sreviceUuid = "0000aaaa-ead2-11e7-80c1-9a214cf093ae";
	std::string _wifiUuid = "00005555-ead2-11e7-80c1-9a214cf093ae";
	std::string _listUuid = "";
	std::string _statusUuid = "";
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
	/** SSIDs of local WiFi networks */
	String ssidPrim;
	String ssidSec;
	/** Password for local WiFi network */
	String pwPrim;
	String pwSec;

	/** Characteristic for digital output */
	BLECharacteristic *pCharacteristicWiFi;
	/** Characteristic for found WiFi list */
	BLECharacteristic *pCharacteristicList;
	/** Characteristic for connection status */
	BLECharacteristic *pCharacteristicStatus;
	/** BLE Advertiser */
	BLEAdvertising *pAdvertising;
	/** BLE Service */
	BLEService *pService;
	/** BLE Server */
	BLEServer *pServer;

	/** Unique device name */
	String apName = "ESP32-xxxxxxxxxxxx";

	void init(String _sreviceUuid = "0000aaaa-ead2-11e7-80c1-9a214cf093ae", String _wifiUuid = "00005555-ead2-11e7-80c1-9a214cf093ae", String _listUuid = "", String _statusUuid = "")
	{
		apName = createName();
	}

	/**
	 * initBLE
	 * Initialize BLE service and characteristic
	 * Start BLE server and service advertising
	 */
	void initBLE()
	{
		// Initialize BLE and set output power
		BLEDevice::init(apName.c_str());
		BLEDevice::setPower(ESP_PWR_LVL_P7);

		// Create BLE Server
		pServer = BLEDevice::createServer();

		// Set server callbacks
		pServer->setCallbacks(new MyServerCallbacks());

		// Create BLE Service
		pService = pServer->createService(BLEUUID(_sreviceUuid), 20);

		// Create BLE Characteristic for WiFi settings
		pCharacteristicWiFi = pService->createCharacteristic(
			BLEUUID(_wifiUuid),
			// WIFI_UUID,
			BLECharacteristic::PROPERTY_READ |
				BLECharacteristic::PROPERTY_WRITE);
		pCharacteristicWiFi->setCallbacks(new MyCallbackHandler());

		// Create BLE characteristic for found SSIDs
		pCharacteristicList = pService->createCharacteristic(
			BLEUUID(_listUuid),
			BLECharacteristic::PROPERTY_READ);
		pCharacteristicList->setCallbacks(new ListCallbackHandler());

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
	}

	int publicWifiScan()
	{
		apNum = actualWiFiScan();
		return apNum;
	}

	/**
	 scanWiFi
	 Scans for available networks 
	 and decides if a switch between
	 allowed networks makes sense

	 @return <code>bool</code>
	        True if at least one allowed network was found
	*/
	bool scanWiFi()
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
};

/**
		 * MyServerCallbacks
		 * Callbacks for client connection and disconnection
		 */
class MyServerCallbacks : public BLEServerCallbacks
{
	Ble_WiFiManager bleWifiManager;
	// TODO this doesn't take into account several clients being connected
	void onConnect(BLEServer *pServer)
	{
		Serial.println("BLE client connected");
		bleWifiManager.deviceConnected = true;
	};

	void onDisconnect(BLEServer *pServer)
	{
		Serial.println("BLE client disconnected");
		bleWifiManager.deviceConnected = false;
		bleWifiManager.pAdvertising->start();
	}
};

/**
		 * MyCallbackHandler
		 * Callbacks for BLE client read/write requests
		 */
class MyCallbackHandler : public BLECharacteristicCallbacks
{
	Ble_WiFiManager bleWifiManager;
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
			value[index] = (char)value[index] ^ (char)apName[keyIndex];
			keyIndex++;
			if (keyIndex >= strlen(apName))
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
				bleWifiManager.ssidPrim = jsonIn["ssidPrim"].as<String>();
				bleWifiManager.pwPrim = jsonIn["pwPrim"].as<String>();
				bleWifiManager.ssidSec = jsonIn["ssidSec"].as<String>();
				bleWifiManager.pwSec = jsonIn["pwSec"].as<String>();

				Preferences preferences;
				preferences.begin("WiFiCred", false);
				preferences.putString("ssidPrim", bleWifiManager.ssidPrim);
				preferences.putString("ssidSec", bleWifiManager.ssidSec);
				preferences.putString("pwPrim", bleWifiManager.pwPrim);
				preferences.putString("pwSec", bleWifiManager.pwSec);
				preferences.putBool("valid", true);
				preferences.end();

				Serial.println("Received over bluetooth:");
				Serial.println("primary SSID: " + bleWifiManager.ssidPrim + " password: " + bleWifiManager.pwPrim);
				Serial.println("secondary SSID: " + bleWifiManager.ssidSec + " password: " + bleWifiManager.pwSec);
				bleWifiManager.connStatusChanged = true;
				bleWifiManager.hasCredentials = true;
			}
			else if (jsonIn.containsKey("erase"))
			{
				Serial.println("Received erase command");
				Preferences preferences;
				preferences.begin("WiFiCred", false);
				preferences.clear();
				preferences.end();
				bleWifiManager.connStatusChanged = true;
				bleWifiManager.hasCredentials = false;
				bleWifiManager.ssidPrim = "";
				bleWifiManager.pwPrim = "";
				bleWifiManager.ssidSec = "";
				bleWifiManager.pwSec = "";

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
		jsonOut["ssidPrim"] = bleWifiManager.ssidPrim;
		jsonOut["pwPrim"] = bleWifiManager.pwPrim;
		jsonOut["ssidSec"] = bleWifiManager.ssidSec;
		jsonOut["pwSec"] = bleWifiManager.pwSec;
		// Convert JSON object into a string
		jsonOut.printTo(wifiCredentials);

		// encode the data
		int keyIndex = 0;
		Serial.println("Stored settings: " + wifiCredentials);
		for (int index = 0; index < wifiCredentials.length(); index++)
		{
			wifiCredentials[index] = (char)wifiCredentials[index] ^ (char)apName[keyIndex];
			keyIndex++;
			if (keyIndex >= strlen(apName))
				keyIndex = 0;
		}
		bleWifiManager.pCharacteristicWiFi->setValue((uint8_t *)&wifiCredentials[0], wifiCredentials.length());
		jsonBuffer.clear();
	}
};

/** ListCallbackHandler
 * callback for SSID list read request
 */
class ListCallbackHandler : public BLECharacteristicCallbacks
{
	Ble_WiFiManager bleWifiManager;

	// {"SSID":["","","","","","","","","",""]} + 10 x 32 bytes for 10 SSIDs, and some spare
	StaticJsonBuffer<500> ssidBuffer;

	void onRead(BLECharacteristic *pCharacteristic)
	{
		Serial.println("BLE onRead request");
		String wifiSSIDsFound;

		byte counter = 0;

		while (!bleWifiManager.apNum && counter < 20)
		{
			if (millis() - bleWifiManager.apScanTime > 10000)
				bleWifiManager.publicWifiScan();
			counter++;
			delay(500);
		}

		/** Json object for outgoing data */
		JsonObject &jsonOut = ssidBuffer.createObject();
		JsonArray &SSID = jsonOut.createNestedArray("SSID");
		for (int i = 0; i < bleWifiManager.apNum && i < 10; i++)
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
		bleWifiManager.pCharacteristicList->setValue((uint8_t *)&wifiSSIDsFound[0], wifiSSIDsFound.length());
		ssidBuffer.clear();
	}
};

/** Build time */
const char compileDate[] = __DATE__ " " __TIME__;

// List of Service and Characteristic UUIDs
// service & wifi uuid are what's used in the original sketch, to maintain compatibility
// list & status uuid were randomly generated using https://www.uuidgenerator.net/
#define SERVICE_UUID "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID "00005555-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_LIST_UUID "1d338124-7ddc-449e-afc7-67f8673a1160"
#define WIFI_STATUS_UUID "5b3595c4-ad4f-4e1e-954e-3b290cc02eb0"

/** Callback for receiving IP address from AP */
void gotIP(system_event_id_t event)
{
	isConnected = true;
	connStatusChanged = true;
	/** Check if ip corresponds to 1st or 2nd configured SSID 
	 * takes semaphore, sets (uint16_t)sendVal, and gives semaphore
	*/
	String connectedSSID = WiFi.SSID();
	xSemaphoreTake(connStatSemaphore, portMAX_DELAY);
	if (connectedSSID == ssidPrim)
	{
		// Serial.println("connected to primary SSID");
		sendVal = 0x0001;
	}
	else if (connectedSSID == ssidSec)
	{
		sendVal = 0x0002;
	}
	xSemaphoreGive(connStatSemaphore);
}

/** Callback for connection loss */
void lostCon(system_event_id_t event)
{
	isConnected = false;
	connStatusChanged = true;
	/** if disconnected, take semaphore, set (uint16_t)sendVal = 0, give semaphore */
	xSemaphoreTake(connStatSemaphore, portMAX_DELAY);
	sendVal = 0x0000;
	xSemaphoreGive(connStatSemaphore);
}

/**
 * Start connection to AP
 */
void connectWiFi()
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
		WiFi.begin(ssidPrim.c_str(), pwPrim.c_str());
	}
	else
	{
		Serial.println(ssidSec);
		WiFi.begin(ssidSec.c_str(), pwSec.c_str());
	}
}

void setup()
{
	// Create unique device name
	createName();

	// Initialize Serial port
	Serial.begin(115200);
	// Send some device info
	Serial.print("Build: ");
	Serial.println(compileDate);

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

	Preferences preferences;
	preferences.begin("WiFiCred", false);
	bool hasPref = preferences.getBool("valid", false);
	if (hasPref)
	{
		ssidPrim = preferences.getString("ssidPrim", "");
		ssidSec = preferences.getString("ssidSec", "");
		pwPrim = preferences.getString("pwPrim", "");
		pwSec = preferences.getString("pwSec", "");

		Serial.printf("%s,%s,%s,%s\n", ssidPrim, pwPrim, ssidSec, pwSec);

		if (ssidPrim.equals("") || pwPrim.equals("") || ssidSec.equals("") || pwPrim.equals(""))
		{
			Serial.println("Found preferences but credentials are invalid");
		}
		else
		{
			Serial.println("Read from preferences:");
			Serial.println("primary SSID: " + ssidPrim + " password: " + pwPrim);
			Serial.println("secondary SSID: " + ssidSec + " password: " + pwSec);
			hasCredentials = true;
		}
	}
	else
	{
		Serial.println("Could not find preferences, need send data over BLE");
	}
	preferences.end();

	// Start BLE server
	initBLE();

	if (hasCredentials)
	{
		apScanTime = millis();
		// Check for available AP's
		if (!scanWiFi())
		{
			Serial.println("Could not find any AP");
		}
		else
		{
			// If AP was found, start connection
			connectWiFi();
		}
	}
}

void loop()
{
	if (connStatusChanged)
	{
		if (isConnected)
		{
			Serial.print("Connected to AP: ");
			String connectedSSID = WiFi.SSID();
			xSemaphoreTake(connStatSemaphore, portMAX_DELAY);
			if (sendVal == 1)
				Serial.println("connected to primary SSID");
			else if (sendVal == 2)
				Serial.println("Connected to secondary SSID");
			xSemaphoreGive(connStatSemaphore);
			Serial.print(connectedSSID);
			Serial.print(" with IP: ");
			Serial.print(WiFi.localIP());
			Serial.print(" RSSI: ");
			Serial.println(WiFi.RSSI());
		}
		else
		{
			if (hasCredentials)
			{
				Serial.println("Lost WiFi connection");
				// Received WiFi credentials
				if (!scanWiFi())
				{ // Check for available AP's
					Serial.println("Could not find any AP");
				}
				else
				{ // If AP was found, start connection
					connectWiFi();
				}
			}
		}
		connStatusChanged = false;
	}
}
