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

// Период и история
const unsigned long INTERVAL = 5000; // 5 секунд
const int MAX_HISTORY = 2880;        // 4 часа
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

// Функция тренда
String getTrend(float now, float past) {
  float delta = now - past;
  if (delta > 0.3) return "H"; // рост
  if (delta < -0.3) return "L"; // падение
  return "S"; // стабильно
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
    Serial.println("\nWiFi OK, IP:");
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

// OTA
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

    // Сохраняем в историю
    history[currentIndex] = {millis(), temp, hum, pres};
    currentIndex = (currentIndex + 1) % MAX_HISTORY;
    if (currentIndex == 0) bufferFilled = true;

    // Сравнение с 3 минутами назад (36 шагов)
    int compareIndex = currentIndex - 36;
    if (compareIndex < 0) compareIndex += MAX_HISTORY;

    SensorReading now = history[(currentIndex - 1 + MAX_HISTORY) % MAX_HISTORY];
    SensorReading past = bufferFilled || currentIndex >= 36 ? history[compareIndex] : now;

    String tT = getTrend(now.temperature, past.temperature);
    String hT = getTrend(now.humidity, past.humidity);
    String pT = getTrend(now.pressure, past.pressure);

    // OLED вывод
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    
    display.printf(" Data: \n");
    display.printf("T: %.1f C %s\n", now.temperature, tT.c_str());
    display.printf("H: %.1f %% %s\n", now.humidity, hT.c_str());
    display.printf("P: %.1f hPa %s\n", now.pressure, pT.c_str());
    display.display();
  }
}
