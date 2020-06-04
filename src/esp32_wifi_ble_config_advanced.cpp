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
// #include <WiFiManager.h>

#include "ble_WiFiManager.h"

/** Build time */
const char compileDate[] = __DATE__ " " __TIME__;

// List of Service and Characteristic UUIDs
// service & wifi uuid are what's used in the original sketch, to maintain compatibility
// list & status uuid were randomly generated using https://www.uuidgenerator.net/
#define SERVICE_UUID "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID "00005555-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_LIST_UUID "1d338124-7ddc-449e-afc7-67f8673a1160"
#define WIFI_STATUS_UUID "5b3595c4-ad4f-4e1e-954e-3b290cc02eb0"

BLE_WIFI_CONFIG_CREATE_INSTANCE(bleWifi);

void setup()
{
	// Initialize Serial port
	Serial.begin(115200);
	// Send some device info
	Serial.print("Build: ");
	Serial.println(compileDate);

		// Start BLE server
	bleWifi.init();

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
