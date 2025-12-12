// 29.07 изменение вывода на экран + время, изменение логики
// 18.08 Микроклимат, веб-страницы / /temp /hum /pres, пин реле D2
// 19.08 массив клавиатуры
// 13.12 АЕ3000 Коробочка эдишн. Кнопка, экран, реле, питание, BME280(температура, влажность и давление) и все в распределительной коробке с удлинителем на 3 слота 10А ^
// + Wi-Fi, сайт/апи, тг бот, ntp и OTA

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include <WiFiClientSecureBearSSL.h>
#include <UniversalTelegramBot.h>

#include <ESP8266WebServer.h>

#include "config.h"  

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA D5
#define OLED_SCL D6

#define RELAY_PIN D2  // GPIO4
#define BUTTON_PIN D1 

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 180000);


std::unique_ptr<BearSSL::WiFiClientSecure> tgClient;
UniversalTelegramBot* bot = nullptr;
unsigned long lastBotPoll = 0;
const unsigned long BOT_POLL_INTERVAL = 2200; // мс

ESP8266WebServer server(80);

bool relayState = false;             
unsigned long lastButtonTime = 0;    
const unsigned long DEBOUNCE = 10;  

bool showSimpleScreen = false;
unsigned long lastToggleTime = 0;
const unsigned long TOGGLE_INTERVAL = 10000;


const unsigned long INTERVAL = 5000;
const int  MAX_HISTORY = 121;
const unsigned long TREND_PERIOD_SEC = 300;
int currentIndex = 0;
unsigned long lastUpdate = 0;

struct SensorReading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  float pressure;
};
SensorReading history[MAX_HISTORY];

float totalTemp = 0, totalHum = 0, totalPres = 0;
unsigned long totalCount = 0;


float lastTemp = NAN, lastHum = NAN, lastPres = NAN;


const char KB_MICRO[] = "[[\"В спальне\"]]";

String trendTempText = "стабильно"; 
String trendHumText  = "стабильно";   
String trendPresText = "стабильно";   

String trendRu(const String& code) {   
  if (code == "H") return "растет";
  if (code == "L") return "падает";
  return "стабильно";
}

void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);  // активный LOW
  Serial.printf("[RELAY] now: %s (pin=%d)\n", on ? "ON" : "OFF", digitalRead(RELAY_PIN));
}

void toggleRelay() {
  setRelay(!relayState);
}

void i2cScan() {
  Serial.println(F("\n[I2C] Scan..."));
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  - found 0x%02X\n", addr);
      count++;
      delay(2);
    }
  }
  if (count == 0) Serial.println(F("  (no devices)"));
  Serial.println(F("[I2C] Scan done.\n"));
}

void printBMEToSerial(float t, float h, float p) {
  Serial.print(F("[BME/BMP] T="));
  if (isnan(t)) Serial.print(F("nan")); else Serial.printf("%.2f C", t);
  Serial.print(F("  H="));
  if (isnan(h)) Serial.print(F("nan")); else Serial.printf("%.2f %%", h);
  Serial.print(F("  P="));
  if (isnan(p)) Serial.print(F("nan")); else Serial.printf("%.2f hPa", p);
  Serial.println();
}

String getTrend(float now, float ref) {
  float d = now - ref;
  if (d > 0.3) return "H";
  if (d < -0.3) return "L";
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
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    display.println("WiFi OK");
    display.print("IP:"); display.println(WiFi.localIP());
    display.display();
    delay(500);
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



String htmlWrap(const String& title, const String& body) {
  String s = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>");
  s += title;
  s += F("</title><style>"
         "body{"
         "  font-family:sans-serif;"
         "  max-width:720px;"
         "  margin:20px auto;"
         "  padding:0 12px;"
         "  text-align:center;"          
         "  background: url('https://cojo.ru/wp-content/uploads/2022/12/anime-fon-luna-1.webp') no-repeat center center fixed,"
         "              url('https://192.168.1.123/static/010.jpg') no-repeat center center fixed;"
         "  background-size: cover;"
         "}"
         "h1{font-size:1.6rem} .v{font-size:2.2rem} a{color:#0a74da;text-decoration:none}"
         "</style>"
         "<meta http-equiv='refresh' content='10'>"
         "</head><body>");
  s += body;
  s += F("<hr><p><a href='/'>Сводка</a> • <a href='/temp'>Температура</a> • <a href='/hum'>Влажность</a> • <a href='/pres'>Давление</a></p>"
         "</body></html>");
  return s;
}

void handleRoot() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "<h1>Микроклимат</h1>"
           "<p class='v'>T: %s&nbsp;°C<br>H: %s&nbsp;%%<br>P: %s&nbsp;hPa</p>",
           isnan(lastTemp) ? "—" : String(lastTemp, 2).c_str(),
           isnan(lastHum)  ? "—" : String(lastHum,  1).c_str(),
           isnan(lastPres) ? "—" : String(lastPres, 1).c_str());
  server.send(200, "text/html; charset=utf-8", htmlWrap("Сводка", buf));
}
void handleTemp() {
  String v = isnan(lastTemp) ? "—" : String(lastTemp, 2);
  server.send(200, "text/html; charset=utf-8",
              htmlWrap("Температура", "<h1>Температура</h1><p class='v'>"+v+" °C</p>"));
}
void handleHum() {
  String v = isnan(lastHum) ? "—" : String(lastHum, 1);
  server.send(200, "text/html; charset=utf-8",
              htmlWrap("Влажность", "<h1>Влажность</h1><p class='v'>"+v+" %</p>"));
}
void handlePres() {
  String v = isnan(lastPres) ? "—" : String(lastPres, 1);
  server.send(200, "text/html; charset=utf-8",
              htmlWrap("Давление", "<h1>Давление</h1><p class='v'>"+v+" hPa</p>"));
}

void handleRelayOn() {
  setRelay(true);  
  server.send(200, "text/html; charset=utf-8",
              htmlWrap("Реле", "<h1>Реле включено</h1><p><a href=\"/\">Назад</a></p>"));
}

void handleRelayOff() {
  setRelay(false); 
  server.send(200, "text/html; charset=utf-8",
              htmlWrap("Реле", "<h1>Реле выключено</h1><p><a href=\"/\">Назад</a></p>"));
}
  
void handleButton() {
  static bool lastReading = HIGH;      
  static bool stableState = HIGH;      
  static unsigned long lastChange = 0; 

  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }

  if (millis() - lastChange >= DEBOUNCE) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        toggleRelay();
        Serial.println("Кнопка");
      }
    }
  }
}

void setupWeb() {

  server.on("/", handleRoot);
  server.on("/temp", handleTemp);
  server.on("/hum", handleHum);
  server.on("/pres", handlePres);

  server.on("/relay/on", handleRelayOn);
  server.on("/relay/off", handleRelayOff);

  server.begin();
  Serial.println(F("[WEB] HTTP server started on :80"));
}



String makeSummaryText() {
  String t = F("🌡 <b>В спальне:</b>\n");

  t += F("T: ");
  t += isnan(lastTemp) ? "—" : String(lastTemp, 2);
  t += F(" °C");
  if (!isnan(lastTemp)) { t += F(" ("); t += trendTempText; t += F(")"); }  
  t += '\n';

  t += F("H: ");
  t += isnan(lastHum) ? "—" : String(lastHum, 1);
  t += F(" %");
  if (!isnan(lastHum)) { t += F(" ("); t += trendHumText; t += F(")"); }    
  t += '\n';

  t += F("P: ");
  t += isnan(lastPres) ? "—" : String(lastPres, 1);
  t += F(" hPa");
  if (!isnan(lastPres)) { t += F(" ("); t += trendPresText; t += F(")"); }  

  return t;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text    = bot->messages[i].text;
    String from    = bot->messages[i].from_name;

    Serial.printf("[TG] msg from %s (%s): %s\n", from.c_str(), chat_id.c_str(), text.c_str());

    // реагируем только на определённые сообщения
    if (text == "В спальне" || text == "/microclimate" || text == "/start" || text == "/kb") {
      String payload = makeSummaryText();
      bot->sendMessageWithReplyKeyboard(chat_id, makeSummaryText(), "HTML", KB_MICRO, true, false, false);
    }
  }
}

void setupTelegram() {
  tgClient.reset(new BearSSL::WiFiClientSecure());
  tgClient->setInsecure();              
  tgClient->setBufferSizes(2048, 1024); 

  bot = new UniversalTelegramBot(botToken364, *tgClient);

  Serial.println(F("[TG] Bot ready."));
  if (WiFi.status() == WL_CONNECTED) {
    bot->sendMessageWithReplyKeyboard(chatId5,
        "Напиши «В спальне».",
        "", KB_MICRO, true, false, false);
  }
}

/*
void toggleRelay() {
  relayState = !relayState;
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
  Serial.printf("[RELAY] now: %s\n", relayState ? "ON" : "OFF");
}
*/

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Booting..."));

  pinMode(RELAY_PIN, OUTPUT);
//  digitalWrite(RELAY_PIN, LOW);
  setRelay(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP); 

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  i2cScan();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED error"));
    while (true) {
      Serial.println(F("Retry OLED in 3s..."));
      delay(3000);
      if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) break;
    }
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AE PRTBL");
  display.display();
  delay(800);

  bool bme_ok = bme.begin(0x76);
  if (!bme_ok) {
    Serial.println(F("BME/BMP @0x76 not found, try 0x77..."));
    bme_ok = bme.begin(0x77);
  }
  if (!bme_ok) {
    Serial.println(F("BME280 error (no device 0x76/0x77)"));
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("BME280 error");
    display.display();
  } else {
    uint8_t id = bme.sensorID();
    Serial.printf("BME/BMP detected, chipID=0x%02X\n", id);
  }

  setupWiFi();
  setupOTA();
  setupWeb();
  setupTelegram();


  lastTemp = bme.readTemperature();
  lastHum  = bme.readHumidity();
  lastPres = bme.readPressure() / 100.0;
  Serial.println(F("[INIT read]"));
  printBMEToSerial(lastTemp, lastHum, lastPres);
}

void loop() {

  ArduinoOTA.handle();
  server.handleClient(); 

  handleButton();

  if (WiFi.status() == WL_CONNECTED && millis() - lastBotPoll > BOT_POLL_INTERVAL) {
    lastBotPoll = millis();
    int numNew = bot->getUpdates(bot->last_message_received + 1);
    while (numNew) {
      handleNewMessages(numNew);
      numNew = bot->getUpdates(bot->last_message_received + 1);
    }
  }

  if (millis() - lastToggleTime >= TOGGLE_INTERVAL) {
    showSimpleScreen = !showSimpleScreen;
    lastToggleTime = millis();
  }

  if (millis() - lastUpdate >= INTERVAL) {
    lastUpdate = millis();

    float temp = bme.readTemperature();
    float hum  = bme.readHumidity();
    float pres = bme.readPressure() / 100.0;
    unsigned long nowSec = millis() / 1000;

    if (isnan(temp) || isnan(pres)) {
      Serial.println(F("[WARN] NaN from sensor; retry once..."));
      delay(10);
      temp = bme.readTemperature();
      hum  = bme.readHumidity();
      pres = bme.readPressure() / 100.0;
    }

    lastTemp = temp;
    lastHum  = hum;
    lastPres = pres;

    //printBMEToSerial(temp, hum, pres);

    history[currentIndex] = { nowSec, temp, hum, pres };
    currentIndex = (currentIndex + 1) % MAX_HISTORY;


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

    String tT = found ? getTrend(temp, past.temperature) : "St";
    String hT = found ? getTrend(hum,  past.humidity)    : "St";
    String pT = found ? getTrend(pres, past.pressure)    : "St";

    trendTempText = trendRu(tT);
    trendHumText  = trendRu(hT);
    trendPresText = trendRu(pT);

    totalTemp += temp; totalHum += hum; totalPres += pres; totalCount++;
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
 