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

#ifndef BLE_WIFIMANAGER_H
#define BLE_WIFIMANAGER_H

#ifdef WiFiManager_h
#define MANAGE_WIFI true
#elif
#define MANAGE_WIFI false
#endif

#if ARDUINOJSON_VERSION_MAJOR!=5
#error ArduinoJson v. 5 is required
#endif


#endif