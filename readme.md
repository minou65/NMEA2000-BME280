# NMEA2000-BM280 environmental sensor

<img src="img/IMG_0300.PNG" width="600" alt="garmin">

## Table of contents
- [NMEA2000-BM280 environmental sensor](#nmea2000-bm280-environmental-sensor)
  - [Table of contents](#table-of-contents)
  - [Description](#description)
  - [Schema](#schema)
  - [NMEA 2000](#nmea-2000)
  - [Librarys](#librarys)
  - [Required hardware](#required-hardware)
  - [Configuration](#configuration)
    - [NMEA 2000 Settings](#nmea-2000-settings)
      - [Instance](#instance)
      - [SID](#sid)
    - [Temperatur source](#temperatur-source)
    - [Humidity source](#humidity-source)
  - [WiFi](#wifi)
    - [Default password](#default-password)
    - [Default IP address](#default-ip-address)
    - [OTA](#ota)
    - [Configuration options](#configuration-options)
      - [Thing name](#thing-name)
      - [AP password](#ap-password)
      - [WiFi SSID](#wifi-ssid)
      - [WiFi password](#wifi-password)
      - [AP offline mode after (minutes)](#ap-offline-mode-after-minutes)
  - [Blinking codes](#blinking-codes)
  - [Reset](#reset)

## Description
This device measures temperature, humidity, and air pressure. The dew point and perceived temperature are also calculated. The sensor used is a BM280. The values are transmitted as NMEA 2000 messages via an NMEA bus. Device configuration is done through a website, and real-time values can also be viewed on a website in addition to the NMEA bus. On the configuration page, there is a link available for convenient firmware updates.

the BME280 is a versatile environmental sensor that provides accurate measurements of temperature, humidity, and barometric pressure.
- Temperatures ranging from -40°C to +85°C with a high degree of accuracy
- relative humidity in the range of 0% to 100%
- pressure sensor data is in hPa (hectopascals) within the range of 300 hPa to 1100 hPa at temperatures from 0°C to 65°C. The absolute accuracy for pressure measurement is approximately ±1 hPa
  
![homepage](img/homepage.png)

In the \stl directory are two STL files. These can be used to print an appropriate housing on a 3D printer.

<img src="img/housing.png" width="300" alt="housing">

## Schema
<img src="sch/schema.png" width="800" alt="housing">

## NMEA 2000
Depending on the temperature source, one of the following PNGs are sent

- 130310, // Environmental Parameters - DEPRECATED
- 130312, // Temperature - DEPRECATED
- 130316, // Temperature, Extended Range

For humidity the following PGN is sent
- 130313, // Humidity

and for pressure
- 130314, // Pressure

## Librarys
- [Adafruit BME280 Library](https://github.com/adafruit/Adafruit_BME280_Library)
- [Adafruit Unified Sensor](https://github.com/adafruit/Adafruit_Sensor)
- [NMEA2000](https://github.com/ttlappalainen/NMEA2000)
- [NMEA200_ESP32](https://github.com/ttlappalainen/NMEA2000_esp32)
- [AsyncTCP (1.1.1)](https://github.com/me-no-dev/AsyncTCP.git)
- [ESPAsyncWebServer (1.2.3)](https://github.com/me-no-dev/ESPAsyncWebServer)
- [ArduionJSON (6.21.2)](https://github.com/bblanchon/ArduinoJson)
- [Webserial (1.4.0)](https://github.com/ayushsharma82/WebSerial)
- [IotWebConf](https://github.com/minou65/IotWebConf)
- [IotWebConfAsync](https://github.com/minou65/IotWebConfAsync)
- [IotWebRoot](https://github.com/minou65/IotWebRoot)

## Part list
| Part | Value | Supplier |
| --- | --- | --- |
| D1 | 1N4001 | Reichelt |
| ESP1 | ESP32DEVKITV1 | [ebay](https://www.ebay.ch/itm/204191675506?var=504772734176) |
| IC1 | R-78E05-1.0 | Reichelt |
| IC3 | MCP2562 | Reichelt oder [ebay](https://www.ebay.ch/itm/364610349378) |
| X1 | AKL 057-03 | Reichelt |
| X2 | AKL 057-02 | Reichelt |
| X3 | PSS 254/4G | Reichelt |
| | BME280 | ebay |

## Configuration
### NMEA 2000 Settings

#### Instance
This should be unique at least on one device. May be best to have it unique over all devices sending this PGN. A total of 5 instances are occupied by the device. Starting with the number set here.

#### SID
Sequence identifier. In most cases you can use just 255 for SID. The sequence identifier field is used to tie different PGNs data together to same sampling or calculation time.

### Temperatur source
One of the following temperature sources can be selected
- Sea water temperature
- Outside temperature
- Inside temperature
- Engine room temperature
- Main cabin temperature
- Live well temperature
- Bait well temperature
- Refrigeration temperature
- Heating system temperature
- Freezer temperature
- Exhaust gas temperature
- Shaft seal temparature

### Humidity source
can be one of the following location
- inside
- outside
- unknown

## WiFi
### Default password
When not connected to an AP the default password is 123456789

### Default IP address
When in AP mode, the default IP address is 192.168.4.1

### OTA
OTA is enabled, use default IP address or if connected to a AP the correct address.
Port is the default port.

### Configuration options
After the first boot, there are some values needs to be set up.
These items are maked with __*__ (star) in the list below.

#### Thing name
Please change the name of the device to a name you think describes it the most. It is advised to incorporate a location here in case you are planning to set up multiple devices in the same area. You should only use english letters, and the "_" underscore character. Thus, must not
use Space, dots, etc. E.g. `lamp_livingroom` __*__

#### AP password
This password is used, when you want to access the device later on. You must provide a password with at least 8, at most 32 characters. You are free to use any characters, further more you are encouraged to pick a password at least 12 characters long containing at least 3 character classes. __*__

#### WiFi SSID
The name of the WiFi network you want the device to connect to. __*__

#### WiFi password
The password of the network above. Note, that unsecured passwords are not supported in your protection. __*__

#### AP offline mode after (minutes)
Specify how long the Wi-Fi should remain enabled after turning on the sensor. Valid values are from 0 to 30 minutes. A value of 0 means that Wi-Fi is always enabled. 

## Blinking codes
Prevoius chapters were mentioned blinking patterns, now here is a table summarize the menaning of the blink codes.

| Blinking Pattern | Meaning |
| --- | --- |
| Rapid blinking <\br>(mostly on, interrupted by short off periods) | Entered Access Point mode. This means the device creates its own WiFi network. You can connect to the device with your smartphone or WiFi capable computer. |
| Alternating on/off blinking | Trying to connect to the configured WiFi network. |
| Mostly off with occasional short flash | The device is online. |
| Mostly off with occasional long flash | The device is in offline mode |


## Reset
When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
password to buld an AP. (E.g. in case of lost password)

Reset pin is GPIO 13
