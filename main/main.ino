#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <AESLib.h>
// WiFi  configuration
const char* ssid = "";
const char* password = "";
// MQTT configuration
const char* mqtt_server = "";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";
const char* mqtt_topic = "sensors/data";

AESLib aesLib;
// DHT sensor configuration

#define DHTPIN 32
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
// MQ135 and DSM501A sensor pins
#define MQ135PIN 34
#define DSM501APIN 14

WiFiClient espClient;
PubSubClient client(espClient);

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Sampling time for DSM501A
#define SAMPLE_TIME_MS 2000

// AES Encryption Key
byte aes_key[] = { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C };


// General initialization vector (you must use your own IV's in production for full security!!!)
byte aes_iv[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

String encrypt_impl(char* msg, byte iv[]) {
  byte enc_iv_A[N_BLOCK] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  aesLib.set_paddingmode(paddingMode::CMS);
  int msgLen = strlen(msg);
  char encrypted[2 * msgLen] = { 0 };
  aesLib.encrypt64((const byte*)msg, msgLen, encrypted, aes_key, sizeof(aes_key),enc_iv_A);
  return String(encrypted);
}
void aes_init() {
aesLib.set_paddingmode(paddingMode::ZeroLength);  
  
}
void setup() {
  Serial.begin(115200);
  aes_init();
  // Initialize DHT sensor
  dht.begin();
  pinMode(MQ135PIN, INPUT);
  pinMode(DSM501APIN, INPUT);

  // Initialize OLED display
  if (!display.begin(0x3C)) {
    Serial.println(F("SH110X allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(F("..."));
  display.display();

  // Initialize WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.println("Connecting to WiFi...");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println(F("Connecting to WiFi..."));
    display.display();
  }
  Serial.println("Connected to WiFi");
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(F("Connected to WiFi"));
  display.display();

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);
    display.println(F("Connecting to MQTT..."));
    display.display();
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Connected to MQTT");
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0);
      display.println(F("Connected to MQTT"));
      display.display();
    } else {
      Serial.print("Failed to connect, rc=");
      Serial.println(client.state());
      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(0, 0);
      display.println(F("MQTT Failed to connect"));
      display.display();
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  delay(1000);

  // Read data from DHT sensor
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  float f = dht.readTemperature(true);

  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Read data from MQ135 sensor
  int mq135Value = analogRead(MQ135PIN);
  int aqi = concentrationToAQI(mq135Value);

  // Read data from DSM501A sensor
  unsigned long duration = pulseIn(DSM501APIN, LOW, SAMPLE_TIME_MS * 1000);
  float ratio = duration / (SAMPLE_TIME_MS * 10.0);
  float concentration = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;
  int dustPercentage = calculateDustPercentage(concentration);

  // Create JSON document
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["id"] = "1";
  jsonDoc["humidity"] = static_cast<int>(h);
  jsonDoc["temperature_c"] = static_cast<int>(t);
  jsonDoc["temperature_f"] = static_cast<int>(f);
  jsonDoc["AQI"] = aqi;
  jsonDoc["dust_percentage"] = dustPercentage;

  char jsonBuffer[512];
  serializeJson(jsonDoc, jsonBuffer);

  
  // Publish JSON data to MQTT
  client.publish(mqtt_topic, encrypt_impl(jsonBuffer,aes_iv).c_str());

  // Print data to Serial Monitor
  Serial.print("JSON Data: ");
  Serial.println(jsonBuffer);

  // Display data on OLED
  display.clearDisplay();
  display.setTextSize(2);              
  display.setTextColor(SH110X_WHITE);  
  display.setCursor(0, 0);          
  display.println("Temp: " + String(static_cast<int>(t)));
  display.println("Humid:" + String(static_cast<int>(h)));
  display.print("AQI: ");
  display.println(aqi);  
  display.println("Pollu:" + String(static_cast<int>(dustPercentage)) + "%");

  display.display();
  delay(SAMPLE_TIME_MS);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      delay(5000);
    }
  }
}

int concentrationToAQI(float concentration) {
  if (concentration <= 50 )  return concentration;
  if (concentration <= 100) return ((concentration - 50) / 50) * 50 + 50;
  if (concentration <= 150) return ((concentration - 100) / 50) * 50 + 100;
  if (concentration <= 200) return ((concentration - 150) / 50) * 50 + 150;
  if (concentration <= 300) return ((concentration - 200) / 100) * 100 + 200;
  if (concentration <= 400) return ((concentration - 300) / 100) * 100 + 300;
  return 500;
}

const float GOOD_THRESHOLD = 35.0;        // Adjust based on PM2.5 or PM10 standards
const float DANGEROUS_THRESHOLD = 10000;  // Adjust based on PM2.5 or PM10 standards

// Function to calculate air quality index as a percentage
int calculateDustPercentage(float concentration) {
  if (concentration <= GOOD_THRESHOLD) {
    return 0;  // Good air quality
  } else if (concentration >= DANGEROUS_THRESHOLD) {
    return 100;  // Dangerous air quality
  } else {
    // Linear interpolation between good and dangerous thresholds
    return map(concentration, GOOD_THRESHOLD, DANGEROUS_THRESHOLD, 0, 100);
  }
}
