# ESP32 Fan Control System for PC, Server, and Rack Temperature Monitoring

## Description
This project is an open-source system designed to control multiple 12V PWM fans based on temperature readings from DS18B20 sensors. It uses an ESP32 microcontroller and supports MQTT over Wi-Fi to report sensor data and fan speeds. The system ensures continuous operation even when Wi-Fi or MQTT connections fail.

## Features
- **Fan Control:** Adjusts fan speed based on temperature ranges.
- **Temperature Monitoring:** Supports multiple DS18B20 sensors on a single pin.
- **MQTT Integration:** Reports fan speed and temperature data over MQTT, and accepts a manual fan speed or `auto`.
- **Fail-Safe Operation:** Maintains functionality if Wi-Fi or MQTT connectivity is lost. Fans run at full speed if no sensor answers.
- **Configurable Parameters:** User-configurable temperature thresholds, PWM frequency, and more.

## ESP32 only
>[!IMPORTANT]
>This sketch is not compatible with ESP8266 due to the use of the ESP32-specific LEDC API for PWM control. The ESP8266 is not suitable for fans requiring high PWM frequencies above 1kHz, as it lacks support for higher PWM rates.

## License
This project is licensed under the **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)**.

## Hardware Requirements
1. **ESP32 Microcontroller**
2. **DS18B20 Temperature Sensors** (1 or more)
3. **12V PWM Fans** (used Arctic S12038-4K requiring 25kHZ PWM frequency)
5. **12V Power Supply**
6. Resistors and jumper wires for connections

## Software Requirements
- **Arduino IDE**
- ESP32 Board Manager (version 3.x, compile-tested with 3.3.0; 2.x also supported)
- Arduino Libraries:
  - `WiFi`
  - `PubSubClient`
  - `OneWire`
  - `DallasTemperature`

## Installation and Setup
1. Clone or download this repository.
2. Open the provided `.ino` file in the Arduino IDE.
3. Install the required libraries from the Arduino Library Manager.
4. Configure the following user variables in the `USER CONFIGURATION SECTION`:
   - Wi-Fi credentials (`ssid` and `password`)
   - MQTT broker information (`mqtt_server`, `mqtt_port`, etc.)
   - Temperature thresholds (`MIN_TEMP`, `MAX_TEMP`)
   - PWM frequency (`PWM_FREQUENCY`) and polarity (`PWM_INVERTED`)
   - Pins (`FAN_PWM`, `ONE_WIRE_BUS`)
5. Connect the hardware as per the pin definitions in the code.
6. Upload the code to your ESP32.

## How to Use
- The system will automatically adjust fan speed based on temperature readings.
- Monitor the fan speeds and temperatures via the MQTT broker (topic `fan/status`, JSON such as `{"temperature":41.50,"pwm":117,"mode":"auto","sensors":3}`).
- Publish a number `0`-`255` to `fan/control` to set the fan speed manually, or `auto` to return to temperature control. A manual speed is overridden with full speed when `MAX_TEMP` is reached or no sensor answers.
- If the Wi-Fi connection fails, the system will continue to adjust fans based on sensor readings.

## Customization
- **Additional Fans and Sensors:** Sensors on the OneWire pin are detected automatically; the hottest one controls the fans. Wire the PWM inputs of all fans to the `FAN_PWM` pin.
- **PWM Frequency:** Modify the `PWM_FREQUENCY` variable to match your fan requirements.
- **Temperature Thresholds:** Adjust `MIN_TEMP` and `MAX_TEMP` to suit your cooling needs.

## Troubleshooting
1. If no sensor is found (serial monitor shows `No sensor reading (fail-safe)`), ensure the DS18B20 sensors are connected with a pull-up resistor. The built-in resistor is not sufficient, add a 4.7kΩ resistor from the data pin to 3V3.
2. Do not use GPIO12 for the fan PWM line. It is a boot strapping pin and the fan's internal pull-up on it prevents the ESP32 from booting. The default is GPIO25.
3. 4-pin fans pull their PWM input up to 3.3V or 5V. The ESP32 is not 5V tolerant: if your fan pulls up to 5V, drive the line through an NPN transistor or N-MOSFET and set `PWM_INVERTED` to `true`.
4. Verify the MQTT broker details.
5. Check serial monitor output (115200 baud) for debugging information.

## Author
Claude Wolter

---

## Contributions
Feel free to contribute to this project by submitting issues or pull requests.

## Disclaimer
This project is for non-commercial use only. Please adhere to the licensing terms.
