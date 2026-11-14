// ============================================
// ESP32 Fan Control System for PC, Server, and Rack Temperature Monitoring
// Version: 0.2
// Author: Claude Wolter
// License: Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
// Target Board: ESP32
// Compilation: ESP32 Arduino Core 3.x (also builds on 2.x)
//
// Description:
// This project is an open-source system designed to control multiple 12V PWM fans based on temperature readings from DS18B20 sensors.
// It uses an ESP32 microcontroller and supports MQTT over Wi-Fi to report sensor data and fan speeds. The system ensures continuous
// operation even when Wi-Fi or MQTT connections fail.
//
// MQTT:
//   Publish to "fan/control":  a number 0-255  -> manual fan speed (clamped to MIN_FAN_SPEED..MAX_FAN_SPEED)
//                              "auto"          -> back to temperature control
//   Status on "fan/status":    {"temperature":41.50,"pwm":117,"mode":"auto","sensors":3}
//
// WARNING
// The ESP32 is not 5V tolerant. 4-pin PC fans pull their PWM input up internally (to 3.3V or 5V depending on the fan).
// If your fan pulls up to 5V, drive the PWM line through an NPN transistor / N-MOSFET (open collector) and set
// PWM_INVERTED to true.
//
// NOTES
// Do not use GPIO12 for the fan PWM line: it is a boot strapping pin, and the fan's pull-up on it stops the ESP32 from booting.
// The ESP8266 is not suitable for fans requiring high PWM frequencies above 1kHz, as it lacks support for higher PWM rates.
// This sketch is not compatible with ESP8266 due to the use of the ESP32-specific LEDC API for PWM control.
// If no DS18B20 is found, check the wiring and fit a 4.7kΩ pull-up from the data pin to 3V3
// (the ESP32's internal pull-up is too weak for OneWire).
//
// ============================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================
// USER CONFIGURATION SECTION
// ============================

// Wi-Fi Credentials
const char* ssid = "YOUR_SSID";        // Replace with your Wi-Fi SSID
const char* password = "YOUR_PASSWORD"; // Replace with your Wi-Fi password

// MQTT Broker Configuration
const char* mqtt_server = "YOUR_MQTT_BROKER_IP"; // Replace with your MQTT broker IP address
const int mqtt_port = 1883;                      // Default MQTT port
const char* mqtt_topic = "fan/control";         // MQTT topic to receive commands
const char* mqtt_status_topic = "fan/status";   // MQTT topic to publish status
const char* mqtt_client_name = "ESP32Client";   // MQTT client name / host name

// Temperature Control Parameters
const float MIN_TEMP = 35.0; // At or below this temperature the fans run at MIN_FAN_SPEED
const float MAX_TEMP = 55.0; // At or above this temperature the fans run at MAX_FAN_SPEED

// Fan Speed Control
const int MIN_FAN_SPEED = 50;  // Minimum fan speed (PWM value, 0-255)
const int MAX_FAN_SPEED = 255; // Maximum fan speed (PWM value, 0-255)

// PWM
const int PWM_FREQUENCY = 25000;  // Frequency for the PWM signal in Hz (25 kHz is the 4-pin PC fan standard)
const bool PWM_INVERTED = false;  // Set to true if the PWM line is driven through a transistor (which inverts the signal)

// Timers (milliseconds)
const unsigned long controlInterval = 1000;    // Read sensors and update fan speed every second
const unsigned long reportInterval = 30000;    // Report MQTT status every 30 seconds
const unsigned long reconnectInterval = 30000; // Retry Wi-Fi/MQTT every 30 seconds while disconnected

// Pin Definitions
#define FAN_PWM 25         // GPIO25 - PWM control for all fans (all fan PWM inputs wired together)
#define ONE_WIRE_BUS 4     // GPIO4  - DS18B20 data pin (all sensors on the same pin)

// ============================

#define PWM_RESOLUTION 8   // 8 bit -> duty 0-255
#define FAN_CHANNEL 0      // LEDC channel (only used on ESP32 core 2.x)

WiFiClient espClient;
PubSubClient client(espClient);

// Temperature Sensors
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastControlTime = 0;
unsigned long lastReportTime = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
bool wifiWasConnected = false;

bool manualMode = false;         // true while a speed received over MQTT overrides temperature control
int manualPwm = MAX_FAN_SPEED;   // speed requested over MQTT
int currentPwm = MAX_FAN_SPEED;  // speed currently applied
float currentTemp = NAN;         // hottest valid sensor reading, NAN if no sensor answers
int validSensors = 0;

// ---------- Fan PWM ----------

bool setupFanPwm() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(FAN_PWM, PWM_FREQUENCY, PWM_RESOLUTION);
#else
  if (ledcSetup(FAN_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION) == 0) return false;
  ledcAttachPin(FAN_PWM, FAN_CHANNEL);
  return true;
#endif
}

void setFanSpeed(int pwmValue) {
  currentPwm = constrain(pwmValue, 0, 255);
  int duty = PWM_INVERTED ? 255 - currentPwm : currentPwm;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(FAN_PWM, duty);      // core 3.x: ledcWrite takes the PIN, not the channel
#else
  ledcWrite(FAN_CHANNEL, duty);  // core 2.x: ledcWrite takes the channel
#endif
}

// ---------- Temperature ----------

// Reads the result of the previous conversion and starts the next one (non-blocking).
void readTemperatures() {
  float maxTemp = NAN;
  int valid = 0;
  int count = sensors.getDeviceCount();

  for (int i = 0; i < count; i++) {
    float temp = sensors.getTempCByIndex(i);
    if (temp == DEVICE_DISCONNECTED_C || temp < -55.0 || temp > 125.0) continue; // sensor missing or bad read
    valid++;
    if (isnan(maxTemp) || temp > maxTemp) maxTemp = temp;
  }

  currentTemp = maxTemp;
  validSensors = valid;

  if (valid == 0) sensors.begin(); // nothing answered: rescan the bus (sensor may have been plugged in later)
  sensors.requestTemperatures();   // returns immediately, result is read on the next call
}

int pwmForTemperature(float temp) {
  if (isnan(temp)) return MAX_FAN_SPEED; // fail-safe: no working sensor -> full speed
  if (temp <= MIN_TEMP) return MIN_FAN_SPEED;
  if (temp >= MAX_TEMP) return MAX_FAN_SPEED;
  float ratio = (temp - MIN_TEMP) / (MAX_TEMP - MIN_TEMP);
  return MIN_FAN_SPEED + (int)(ratio * (MAX_FAN_SPEED - MIN_FAN_SPEED) + 0.5);
}

void updateFan() {
  int pwmValue = pwmForTemperature(currentTemp);
  // Manual speed from MQTT is used unless there is no sensor or the temperature limit is reached
  if (manualMode && !isnan(currentTemp) && currentTemp < MAX_TEMP) {
    pwmValue = manualPwm;
  }
  setFanSpeed(pwmValue);
}

// ---------- MQTT ----------

void reportStatus() {
  String payload = "{\"temperature\":";
  payload += isnan(currentTemp) ? String("null") : String(currentTemp, 2);
  payload += ",\"pwm\":" + String(currentPwm);
  payload += ",\"mode\":\"" + String(manualMode ? "manual" : "auto") + "\"";
  payload += ",\"sensors\":" + String(validSensors);
  payload += "}";
  client.publish(mqtt_status_topic, payload.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  Serial.print("Message received: ");
  Serial.println(message);

  if (message.equalsIgnoreCase("auto")) {
    manualMode = false;
  } else {
    bool isNumber = message.length() > 0;
    for (unsigned int i = 0; i < message.length(); i++) {
      if (!isDigit(message[i])) isNumber = false;
    }
    if (!isNumber) {
      Serial.println("Ignored: send a number 0-255 or \"auto\".");
      return;
    }
    manualPwm = constrain(message.toInt(), MIN_FAN_SPEED, MAX_FAN_SPEED);
    manualMode = true;
  }

  updateFan();
  reportStatus();
}

// ---------- Connectivity (never blocks fan control for long) ----------

void maintainConnections() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      Serial.println("WiFi lost. Fan control continues offline.");
    }
    if (now - lastWifiAttempt >= reconnectInterval) {
      lastWifiAttempt = now;
      Serial.println("Retrying WiFi...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
    lastMqttAttempt = now - reconnectInterval; // try MQTT right away
  }

  if (!client.connected() && now - lastMqttAttempt >= reconnectInterval) {
    lastMqttAttempt = now;
    Serial.print("Attempting MQTT connection... ");
    if (client.connect(mqtt_client_name)) {
      Serial.println("connected.");
      client.subscribe(mqtt_topic);
      Serial.print("Subscribed to topic: ");
      Serial.println(mqtt_topic);
      reportStatus();
      lastReportTime = now;
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }

  client.loop();
}

// ---------- Arduino entry points ----------

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Fans first: full speed until the first temperature reading is available
  if (!setupFanPwm()) {
    Serial.println("Failed to configure PWM pin");
    while (true) delay(1000); // Halt if configuration fails
  }
  setFanSpeed(MAX_FAN_SPEED);

  // Temperature sensors (non-blocking conversions)
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  Serial.print("DS18B20 sensors found: ");
  Serial.println(sensors.getDeviceCount());

  // Wi-Fi / MQTT: started here, completed in the background by maintainConnections()
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(mqtt_client_name);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  lastWifiAttempt = millis();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setSocketTimeout(5);

  lastControlTime = millis();
}

void loop() {
  maintainConnections();

  unsigned long now = millis();

  // Read temperatures and control fans regardless of Wi-Fi connection
  if (now - lastControlTime >= controlInterval) {
    lastControlTime = now;
    readTemperatures();
    updateFan();

    if (isnan(currentTemp)) {
      Serial.print("No sensor reading (fail-safe), PWM: ");
    } else {
      Serial.print("Max Temp: ");
      Serial.print(currentTemp);
      Serial.print(" °C (");
      Serial.print(validSensors);
      Serial.print(" sensors), Mode: ");
      Serial.print(manualMode ? "manual" : "auto");
      Serial.print(", PWM: ");
    }
    Serial.println(currentPwm);
  }

  if (client.connected() && now - lastReportTime >= reportInterval) {
    lastReportTime = now;
    reportStatus();
  }
}
