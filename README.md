# WiFi configuration over BLE
 
Used to configure WiFi credentials over Bluetooth LE on a ESP32 WROOM.
 
### Requirements:
ArduinoJson v.5 (checked with 5.13.4)

#### Confirmed working envioronments:
* Arduino 1.8.11 & esp32-arduino 1.0.4
* PlatformIO Home 3.1.0, Core 4.2.1, Espressif 32 1.11.2

### Based on Bernd Giescke's (beegee1962) sketch for WiFi configuration over BLE:
Documentation: https://desire.giesecke.tk/index.php/2018/04/06/esp32-wifi-setup-over-ble/
Code: https://bitbucket.org/beegee1962/esp32_wifi_ble_esp32/src/master/
 
### Additions made by Uri Shani (UriShX), 02/2020: 
1. Additional characteristic for getting SSID list over BLE (read only)
2. Additional characteristic for serving connection status as notifications, every 1 second

Published under the MIT license, see [LICENSE.md](https://github.com/UriShX/esp32_wifi_ble_advanced/LICENSE.md)