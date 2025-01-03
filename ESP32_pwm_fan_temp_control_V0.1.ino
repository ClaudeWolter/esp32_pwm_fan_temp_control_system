// ============================================
// ESP32 Fan Control System for PC, Server, and Rack Temperature Monitoring
// Author: Claude Wolter
// License: Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)
// Target Board: ESP32
// Compilation: Tested with ESP32 Arduino Core version 3.0.7
//
// Description:
// This program controls multiple 12V PWM fans based on temperature readings from multiple DS18B20 sensors.
// It uses MQTT over Wi-Fi to report sensor data and fan speeds, with automatic reconnection attempts if Wi-Fi or MQTT fails.
// Fans are controlled by a single PWM pin, and temperature sensors share a common data pin.
// ============================================

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

// Sensor and Fan Configuration
const int NUM_SENSORS = 3;    // Number of DS18B20 temperature sensors
const int NUM_FANS = 3;       // Number of fans

// Instructions:
// If you have more than 3 sensors or fans, increase the respective variables above.
// Ensure you define the correct pins for fans and sensors in the Pin Definitions section.

// Temperature Control Parameters
const float MIN_TEMP = 35.0; // Minimum temperature for lowest fan speed
const float MAX_TEMP = 55.0; // Maximum temperature for full fan speed

// Fan Speed Control
const int MIN_FAN_SPEED = 50;  // Minimum fan speed (PWM value, 0-255)
const int MAX_FAN_SPEED = 255; // Maximum fan speed (PWM value, 0-255)

// PWM Frequency
const int PWM_FREQUENCY = 25000; // Frequency for the PWM signal in Hz. Adjust this based on fan requirements.

// Timers
const unsigned long reportInterval = 30000;      // Report MQTT status every 30 seconds
const unsigned long reconnectInterval = 600000; // Attempt to reconnect Wi-Fi/MQTT every 10 minutes
const int reconnectAttempts = 3;                // Maximum number of reconnection attempts

// ============================

// Pin Definitions
#define FAN_PWM 12         // GPIO12 (D6) - PWM control for all fans
#define ONE_WIRE_BUS 4     // GPIO4 (D2) - Dallas temperature sensors data pin

// LEDC PWM Channel
#define FAN_CHANNEL 0      // PWM channel for all fans

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

WiFiClient espClient;
PubSubClient client(espClient);

// Temperature Sensors
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastReportTime = 0; // Timer for MQTT reporting
unsigned long lastReconnectAttempt = 0; // Timer for reconnect attempts

void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  int attempts = 0;
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED && attempts < reconnectAttempts) {
    delay(5000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to WiFi. Proceeding offline.");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message received: ");
  Serial.println(message);

  int pwmValue = message.toInt();
  pwmValue = constrain(pwmValue, MIN_FAN_SPEED, MAX_FAN_SPEED);

  // Apply PWM value to all fans
  ledcWrite(FAN_CHANNEL, pwmValue);
}

bool reconnect() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
      return true;
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again later");
      return false;
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Initialize temperature sensors
  sensors.begin();

  // Activate internal pullup if supported
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);

  // Configure PWM pin
  if (!ledcAttachChannel(FAN_PWM, PWM_FREQUENCY, 8, FAN_CHANNEL)) {
    Serial.println("Failed to configure PWM channel");
    while (true); // Halt if configuration fails
  }
}

void reportStatus(float maxTemp, int pwmValue) {
  String payload = "{";
  payload += "\"temperature\":" + String(maxTemp);
  payload += ", \"pwm\":" + String(pwmValue);
  payload += "}";
  client.publish(mqtt_status_topic, payload.c_str());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected() && millis() - lastReconnectAttempt > reconnectInterval) {
      lastReconnectAttempt = millis();
      reconnect();
    }
    client.loop();
  }

  // Read temperatures and control fans regardless of Wi-Fi connection
  sensors.requestTemperatures();
  float maxTemp = -127.0;
  for (int i = 0; i < NUM_SENSORS; i++) {
    float temp = sensors.getTempCByIndex(i);
    if (temp > maxTemp) {
      maxTemp = temp;
    }
  }

  int pwmValue = (maxTemp <= MIN_TEMP) ? MIN_FAN_SPEED : (maxTemp >= MAX_TEMP) ? MAX_FAN_SPEED : map(maxTemp, MIN_TEMP, MAX_TEMP, MIN_FAN_SPEED, MAX_FAN_SPEED);
  ledcWrite(FAN_CHANNEL, pwmValue);

  if (WiFi.status() == WL_CONNECTED && millis() - lastReportTime > reportInterval) {
    lastReportTime = millis();
    reportStatus(maxTemp, pwmValue);
  }

  // Always print temperature and PWM for debugging
  Serial.print("Max Temp: ");
  Serial.print(maxTemp);
  Serial.print(" °C, PWM: ");
  Serial.println(pwmValue);

  delay(1000);
}