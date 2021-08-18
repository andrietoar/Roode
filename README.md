# RooDe
People counter working with any smart home system which supports MQTT
Project planning in public Trello Board: https://trello.com/b/Yonlc8Fw

## Hardware
There will be a specific Hardware setup (recommended brands etc.) soon!
* ESP8266 or ESP32
* 1x VL53L1X
* Optional HC-SR501
* Optional 128x32 OLED
* Power Supply
* Encolsure (see .stl files) - will be updated soon!

## Changelog
### Changelog v1.0
#### Additions and Breaking Changes
* ESPHome
* Removes legacy support which includes
   * Arduino plattform
   * MySensors plattform
   * **Non** VL53L1X-Sensors
* VL531L1X TOF Sensor with configurable ROI zones for one-sensor-solution
* Counting people entering and leaving a room with path tracking for improved accuracy
* Sensor threhsold calibration for each zone
* Receiving commands from the the controller e.g softreset
* OLED support (coming in 1.1)
* Sleep mode with Motion Sensor support (coming in 1.1)
* Firmware upgrades over WebUI (esphome)

   
### Changelog v0.9.6
#### Additions
* added full VL53L0X support
* Added OLED brightness config option
* Updated to MySensors@2.3.1
#### Bugfixes and other changes
* measruing speed improvements 
* Fixed receiving and sending message issues
* General bug fixes and improvements
* Changed default PA_LEVEL to HIGH

### Changelog v0.9.5
* Added VL53L0X support
    * Using the Pololu VL53XXX Library
* Huge Code refactor

### Changelog v0.9.4-alpha2:
* added incomplete push button support
    * There is a need of 2 spare interrupt PINS
    * May be working with PinChangeInterrupt library
* minor bugfixes
* rename send() function and changed signature
### Changelog v0.9.4-beta:
* changed IR-calibration hardcoded value to THRESHOLD_X 
* CALIBRATION_VAL to 4000
### Changelog v.0.9.4-release
* Added standard deviation threhsold calculation
* Removed constant THRESHOLD_X
