// 27.07 тренд истории + сравнение со средним значением

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include "config.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA D5
#define OLED_SCL D6

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 180000);

bool showSimpleScreen = false;
unsigned long lastToggleTime = 0;
const unsigned long TOGGLE_INTERVAL = 10000; 

const unsigned long INTERVAL = 5000; 
const int MAX_HISTORY = 121; 
const unsigned long TREND_PERIOD_SEC = 300;

int currentIndex = 0;
bool bufferFilled = false;
unsigned long lastUpdate = 0;

struct SensorReading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  float pressure;
};

SensorReading history[MAX_HISTORY];

float totalTemp = 0;
float totalHum = 0;
float totalPres = 0;
unsigned long totalCount = 0;


String getTrend(float now, float reference) {
  float delta = now - reference;
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
  display.println(" Connecting...");
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
    timeClient.begin();
    timeClient.update();
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

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED error"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AE PRTBL");
  display.display();
  delay(1500);

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

 // Serial.printf("Free heap: %lu\n", ESP.getFreeHeap());

  if (millis() - lastToggleTime >= TOGGLE_INTERVAL) {
    showSimpleScreen = !showSimpleScreen;
    lastToggleTime = millis();
  }

  if (millis() - lastUpdate >= INTERVAL) {
    lastUpdate = millis();

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float pres = bme.readPressure() / 100.0;
    unsigned long nowSec = millis() / 1000;

    // сохранить в буфер
    history[currentIndex] = { nowSec, temp, hum, pres };
    currentIndex = (currentIndex + 1) % MAX_HISTORY;

    // найти точку 5 минут назад
    SensorReading past = { 0, 0, 0, 0 };
    bool found = false;

    for (int i = MAX_HISTORY - 1; i > 0; i--) {
      int idx = (currentIndex - i + MAX_HISTORY) % MAX_HISTORY;
      if (history[idx].timestamp == 0) continue;

      if ((nowSec - history[idx].timestamp) >= TREND_PERIOD_SEC) {
        past = history[idx];
        found = true;
        break;
      }
    }

    // тренды по сравнению с точкой 5 минут назад
    String tT = found ? getTrend(temp, past.temperature) : "St";
    String hT = found ? getTrend(hum,  past.humidity)    : "St";
    String pT = found ? getTrend(pres, past.pressure)    : "St";

    // накопление для средних
    totalTemp += temp;
    totalHum  += hum;
    totalPres += pres;
    totalCount++;

    float avgTemp = totalTemp / totalCount;
    float avgHum  = totalHum  / totalCount;
    float avgPres = totalPres / totalCount;

    unsigned long uptimeSec = millis() / 1000;
    int hh = uptimeSec / 3600;
    int mm = (uptimeSec % 3600) / 60;

    display.clearDisplay();

    if (showSimpleScreen) {
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.printf("T: %.1f\n", temp);
      display.printf("H: %.0f %%\n", hum);
      display.printf("P: %.0f\n", pres);

      if (WiFi.status() == WL_CONNECTED) {
        timeClient.update();
        int ntpHH = timeClient.getHours();
        int ntpMM = timeClient.getMinutes();
        display.setCursor(0, SCREEN_HEIGHT - 14);
        display.setTextSize(2);
        display.printf(" %02d:%02d\n", ntpHH, ntpMM);
      }  
        
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.printf(" DATA %02d:%02d\n", hh, mm);
      display.printf("T: %.1f C %s\n", temp, tT.c_str());
      display.printf(" %.1f \n", avgTemp);
      display.printf("H: %.1f %% %s\n", hum, hT.c_str());
      display.printf(" %.1f\n", avgHum);
      display.printf("P: %.1f %s\n", pres, pT.c_str());
      display.printf(" %.1f\n", avgPres);
    }

    display.display();
  }
}
