// ============================================================
//  Smart Electronic System for Farming
//  Author      : Gaurav Patil (Team Lead) & Team
//  Institution : SSBT College of Engineering & Technology,
//                Bambhori, Jalgaon — KBC NMU
//  Year        : 2025-2026
//  Board       : ESP32 (WROOM-32)
//  IDE         : Arduino IDE with ESP32 Board Support
// ============================================================

#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <RTClib.h>

// ── PIN DEFINITIONS ──────────────────────────────────────────
#define SOIL_PIN        34   // Soil Moisture Sensor (YL-69) — Analog
#define RAIN_PIN        14   // Rain Drop Sensor (YL-83)     — Digital
#define WATER_LEVEL_PIN 32   // Water Level Sensor           — Analog
#define LDR_PIN         33   // LDR Light Sensor             — Analog
#define DHT_PIN          4   // DHT11 Temp & Humidity        — Digital
#define RELAY_PIN       26   // Relay Module (Pump Control)  — Digital
#define MANUAL_BTN_PIN  27   // Manual Override Button       — Digital

// ── SENSOR & MODULE SETUP ────────────────────────────────────
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// LCD Display — I2C address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RTC Module DS3231
RTC_DS3231 rtc;

// ── THRESHOLDS ───────────────────────────────────────────────
#define SOIL_DRY_THRESHOLD   2500   // ADC value above this = soil is dry
#define WATER_LOW_THRESHOLD   500   // ADC value below this = tank is low
#define LDR_NIGHT_THRESHOLD  1000   // ADC value below this = night time

// ── Wi-Fi CREDENTIALS ────────────────────────────────────────
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ── MQTT BROKER SETTINGS ─────────────────────────────────────
const char* mqtt_broker = "broker.hivemq.com";   // Free public broker
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "farm/sensors";

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ── SYSTEM STATE ─────────────────────────────────────────────
bool pumpState      = false;
bool manualOverride = false;
bool rainDetected   = false;
bool waterLow       = false;

// ── SETUP ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("Smart Electronic System for Farming — Initialising...");

  // Pin modes
  pinMode(RELAY_PIN,       OUTPUT);
  pinMode(MANUAL_BTN_PIN,  INPUT_PULLUP);
  pinMode(RAIN_PIN,        INPUT);
  digitalWrite(RELAY_PIN, HIGH);   // Relay OFF by default (active LOW)

  // DHT11
  dht.begin();

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Farming");
  lcd.setCursor(0, 1);
  lcd.print("Initialising...");
  delay(2000);
  lcd.clear();

  // RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found! Check wiring.");
  }

  // Wi-Fi
  connectWiFi();

  // MQTT
  mqttClient.setServer(mqtt_broker, mqtt_port);

  Serial.println("System Ready.");
}

// ── MAIN LOOP ────────────────────────────────────────────────
void loop() {

  // Reconnect MQTT if disconnected
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // ── READ ALL SENSORS ──────────────────────────────────────
  int   soilValue   = analogRead(SOIL_PIN);
  int   waterLevel  = analogRead(WATER_LEVEL_PIN);
  int   ldrValue    = analogRead(LDR_PIN);
  bool  rainPin     = digitalRead(RAIN_PIN);
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  bool  manualBtn   = !digitalRead(MANUAL_BTN_PIN);  // Active LOW

  // Derived states
  rainDetected  = (rainPin == LOW);          // LOW = rain detected
  waterLow      = (waterLevel < WATER_LOW_THRESHOLD);
  bool soilDry  = (soilValue  > SOIL_DRY_THRESHOLD);
  bool isNight  = (ldrValue   < LDR_NIGHT_THRESHOLD);

  // ── IRRIGATION CONTROL LOGIC ──────────────────────────────

  if (manualBtn) {
    // Manual override — toggle pump
    manualOverride = true;
    pumpState = !pumpState;
    delay(500);  // debounce

  } else if (rainDetected) {
    // Rain suppression — stop irrigation
    manualOverride = false;
    pumpState = false;
    Serial.println("STATUS: Rain detected — irrigation suppressed");

  } else if (waterLow) {
    // Water level safety cut-off
    manualOverride = false;
    pumpState = false;
    Serial.println("STATUS: Water level low — pump safety cut-off");

  } else {
    // Automatic threshold-based control
    manualOverride = false;
    if (soilDry) {
      pumpState = true;
      Serial.println("STATUS: Soil dry — pump ON");
    } else {
      pumpState = false;
      Serial.println("STATUS: Soil moist — pump OFF");
    }
  }

  // ── RELAY CONTROL ─────────────────────────────────────────
  // Relay is active LOW: LOW = pump ON, HIGH = pump OFF
  digitalWrite(RELAY_PIN, pumpState ? LOW : HIGH);

  // ── LCD DISPLAY UPDATE ────────────────────────────────────
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("S:");
  lcd.print(soilValue);
  lcd.print(" T:");
  lcd.print((int)temperature);
  lcd.print("C");

  lcd.setCursor(0, 1);
  lcd.print("Pump:");
  lcd.print(pumpState ? "ON " : "OFF");
  lcd.print(rainDetected ? " RAIN" : waterLow ? " LOW" : "     ");

  // ── SERIAL DEBUG ──────────────────────────────────────────
  Serial.println("──────────────────────────────");
  Serial.print("Soil Moisture ADC : "); Serial.println(soilValue);
  Serial.print("Water Level ADC   : "); Serial.println(waterLevel);
  Serial.print("LDR Value         : "); Serial.println(ldrValue);
  Serial.print("Temperature       : "); Serial.print(temperature); Serial.println(" °C");
  Serial.print("Humidity          : "); Serial.print(humidity);    Serial.println(" %");
  Serial.print("Rain Detected     : "); Serial.println(rainDetected ? "YES" : "NO");
  Serial.print("Water Low         : "); Serial.println(waterLow    ? "YES" : "NO");
  Serial.print("Pump State        : "); Serial.println(pumpState   ? "ON"  : "OFF");
  Serial.print("Manual Override   : "); Serial.println(manualOverride ? "YES" : "NO");
  Serial.println("──────────────────────────────");

  // ── MQTT PUBLISH ──────────────────────────────────────────
  String payload = "{";
  payload += "\"soil\":"        + String(soilValue)   + ",";
  payload += "\"temperature\":" + String(temperature) + ",";
  payload += "\"humidity\":"    + String(humidity)    + ",";
  payload += "\"waterLevel\":"  + String(waterLevel)  + ",";
  payload += "\"rain\":"        + String(rainDetected ? "true" : "false") + ",";
  payload += "\"pump\":"        + String(pumpState    ? "true" : "false");
  payload += "}";

  mqttClient.publish(mqtt_topic, payload.c_str());
  Serial.println("MQTT Published: " + payload);

  delay(2000);   // 2 second loop interval
}

// ── HELPER: Wi-Fi CONNECTION ─────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWi-Fi failed — running in offline mode.");
  }
}

// ── HELPER: MQTT RECONNECT ───────────────────────────────────
void reconnectMQTT() {
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 3) {
    Serial.print("Connecting to MQTT broker...");
    String clientId = "ESP32-SmartFarm-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected!");
      mqttClient.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(2000);
    }
    attempts++;
  }
}
