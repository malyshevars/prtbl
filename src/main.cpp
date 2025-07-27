//27.07 ота+bmp280+wifi+история

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include "config.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA D5
#define OLED_SCL D6

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

// История
const unsigned long INTERVAL = 1000; 
const int MAX_HISTORY = 2880;        // 5 ч по 5 сек = 2880
int currentIndex = 0;
bool bufferFilled = false;
unsigned long lastUpdate = 0;

struct SensorReading {
  uint32_t timestamp;     // в секундах
  float temperature;
  float humidity;
  float pressure;
};

SensorReading history[MAX_HISTORY];

// Тренд (delta > 0.3 — изменение)
String getTrend(float now, float past) {
  float delta = now - past;
  if (delta > 0.3) return "H";
  if (delta < -0.3) return "L";
  return "S";
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("WiFi...");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("WiFi connecting...");
  display.display();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    display.println("WiFi OK");
    display.print("IP:");
    display.println(WiFi.localIP());
    display.display();
  } else {
    Serial.println("\nWiFi FAIL");
    display.println("WiFi FAIL");
    display.display();
  }
}

void setupOTA() {
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("OTA Update...");
    display.display();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("OTA Done!");
    display.display();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]\n", error);
  });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED error"));
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("PRTBL AE");
  display.display();
  delay(1500);

  // BME280
  if (!bme.begin(0x76)) {
    Serial.println(F("BME280 error"));
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("BME280 error");
    display.display();
    while (true);
  }

  setupWiFi();
  setupOTA();
}

void loop() {
  ArduinoOTA.handle();

  if (millis() - lastUpdate >= INTERVAL) {
    lastUpdate = millis();

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float pres = bme.readPressure() / 100.0;
    unsigned long nowSec = millis() / 1000;

    // Сохраняем в буфер
    history[currentIndex] = { nowSec, temp, hum, pres };
    currentIndex = (currentIndex + 1) % MAX_HISTORY;
    if (currentIndex == 0) bufferFilled = true;

    // Поиск записи 
    SensorReading now = history[(currentIndex - 1 + MAX_HISTORY) % MAX_HISTORY];
    SensorReading past = now;
    bool foundPast = false;
    for (int i = 1; i < MAX_HISTORY; i++) {
      int idx = (currentIndex - i + MAX_HISTORY) % MAX_HISTORY;  // ⬅️ Идём назад
      if (history[idx].timestamp == 0) break; // пусто — конец валидных данных
      if ((nowSec - history[idx].timestamp) > 5UL * 3600) break; // старше 5 часов — не берём
      past = history[idx];
      foundPast = true;
      break;
    }
  

    String tT = foundPast ? getTrend(now.temperature, past.temperature) : "?";
    String hT = foundPast ? getTrend(now.humidity, past.humidity) : "?";
    String pT = foundPast ? getTrend(now.pressure, past.pressure) : "?";

    // OLED вывод
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);

    unsigned long uptimeSec = millis() / 1000;
    int hh = uptimeSec / 3600;
    int mm = (uptimeSec % 3600) / 60;
    int ss = uptimeSec % 60;
    display.printf("DATA %02d:%02d:%02d\n", hh, mm, ss);

    display.printf("T: %.1f C %s\n", now.temperature, tT.c_str());
    display.printf("H: %.1f %% %s\n", now.humidity, hT.c_str());
    display.printf("P: %.1f hPa %s\n", now.pressure, pT.c_str());
    display.display();

  }
}
