# WiFi configuration over BLE

A esp32-Arduino sketch used to configure WiFi credentials over Bluetooth LE on a ESP32 WROOM. \
A web based app for configuration can be found [here](https://urishx.github.io/Nuxt_esp32_web-ble_wifi_config/), the code lives in [my github repo](https://github.com/UriShX/Nuxt_esp32_web-ble_wifi_config). This app is written in NuxtJS, and is MIT licensed. \
An older version of the web app can be found [here](https://urishx.github.io/esp32_web-ble_wifi_config/), with it's code [on Github](https://github.com/UriShX/esp32_web-ble_wifi_config). This version is written with KnockoutJS and JQuery, and is also MIT licensed, but less secure and the code is harder to follow.

### Requirements:

ArduinoJson v.5 (checked with 5.13.4)

#### Confirmed working environments:

- Arduino 1.8.11 & esp32-arduino 1.0.4
- PlatformIO Home 3.1.0, Core 4.2.1, Espressif 32 1.11.2

### Based on Bernd Giescke's (beegee1962) sketch for WiFi configuration over BLE:

Documentation: https://desire.giesecke.tk/index.php/2018/04/06/esp32-wifi-setup-over-ble/ \
Code: https://bitbucket.org/beegee1962/esp32_wifi_ble_esp32/src/master/

### Additions made by Uri Shani (UriShX), 02/2020:

1. Additional characteristic for getting SSID list over BLE (read only)
2. Additional characteristic for serving connection status as notifications, every 1 second

## Debug in linux (Ubuntu)

**Use escape char before colon in adress ('\\:')**

```bash
$ sudo find -name 'bluetooth'
$ sudo su
$ cd /var/lib/bluetooth/<COLON SEPARATED LAPTOP BT DEVICE ADDRESS>/
$ ls
$ cd <COLON SEPARATED ESP32 BT DEVICE ADDRESS>/
$ ls
$ cat attributes
$ cat info
$ cd ..
$ rm -rf <COLON SEPARATED ESP32 BT DEVICE ADDRESS>/
$ exit
```

Published under the MIT license, see [LICENSE.md](https://github.com/UriShX/esp32_wifi_ble_advanced/LICENSE.md)
