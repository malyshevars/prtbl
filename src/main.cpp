// 29.07 изменение вывода на экран + время, изменение логики
// 18.08 Микроклимат, веб-страницы / /temp /hum /pres, пин реле D2
// 19.08 массив клавиатуры
// 13.12 АЕ3000 Коробочка эдишн. Кнопка, выключатель, экран, реле, питание, BME280(температура, влажность и давление) и все в распределительной коробке с удлинителем на 3 слота 10А ^
// + Wi-Fi, сайт/апи, тг бот, ntp и OTA 
//18.12 наконец работает надежнее
//19.12 добавлен выключатель, конденсатор и резистор (схемотехника), вычещен код - закоментил лишнее, 2 wifi
//20.12 +расписание +тг переделка и изменены делеи
//21,12 +датчик движения (расписание кривое)

#include <Arduino.h>

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

#define RELAY_PIN D2 
#define BUTTON_PIN D1 

#define PIR_PIN D7 

bool useSchedule = false;

volatile bool g_pirEvent = false;
unsigned long lastPirTime = 0;
const unsigned long PIR_COOLDOWN = 59000; 

volatile bool g_btnEvent = false;
volatile uint32_t g_btnIrqMs = 0;

unsigned long lastMotionTime = 0;
const unsigned long LIGHT_TIMEOUT = 20UL * 60UL * 1000UL;


void ICACHE_RAM_ATTR onButtonFall() {
  uint32_t now = millis();
  if (now - g_btnIrqMs > 100) {
    g_btnIrqMs = now;
    g_btnEvent = true;
  }
}

bool relayState = false;             
unsigned long lastButtonTime = 0;    
const unsigned long DEBOUNCE = 1000;  

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_BME280 bme;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 180000);


std::unique_ptr<BearSSL::WiFiClientSecure> tgClient;
UniversalTelegramBot* bot = nullptr;
unsigned long lastBotPoll = 0;
const unsigned long BOT_POLL_INTERVAL = 2200; // мс

ESP8266WebServer server(80);

bool relayBySchedule() {
  if (WiFi.status() != WL_CONNECTED) return false;

  timeClient.update();
  int h = timeClient.getHours();

  bool night = (h >= 17 || h < 3);   // 17:00 – 03:00
  bool morning = (h >= 7 && h < 10); // 07:00 – 10:00

  return night || morning;
}



/*
bool showSimpleScreen = false;
unsigned long lastToggleTime = 0;
const unsigned long TOGGLE_INTERVAL = 10000;
*/

const unsigned long INTERVAL = 5000;
unsigned long lastUpdate = 0;
/*
const int  MAX_HISTORY = 121;
const unsigned long TREND_PERIOD_SEC = 300;
int currentIndex = 0;


struct SensorReading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  float pressure;
};
SensorReading history[MAX_HISTORY];

float totalTemp = 0, totalHum = 0, totalPres = 0;
unsigned long totalCount = 0;
*/

float lastTemp = NAN, lastHum = NAN, lastPres = NAN;


const char KB_MICRO[] = "[[\"Климат\"]," "[\"Включить свет\",\"Выключить свет\"]]";

/*
String trendTempText = "стабильно"; 
String trendHumText  = "стабильно";   
String trendPresText = "стабильно";   


String trendRu(const String& code) {   
  if (code == "H") return "растет";
  if (code == "L") return "падает";
  return "стабильно";
}
*/

void ICACHE_RAM_ATTR onPirRise() {
  g_pirEvent = true;
}

void setRelay(bool on) {
  relayState = on;
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);  // активный LOW
  Serial.printf("[RELAY] now: %s (pin=%d)\n", on ? "ON" : "OFF", digitalRead(RELAY_PIN));
}

void toggleRelay() {
  setRelay(!relayState);
}
/*
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
  Serial.println(F("[I2C] Ok\n"));
}
*/

void printBMEToSerial(float t, float h, float p) {
  Serial.print(F("[BME/BMP] T="));
  if (isnan(t)) Serial.print(F("nan")); else Serial.printf("%.2f C", t);
  Serial.print(F("  H="));
  if (isnan(h)) Serial.print(F("nan")); else Serial.printf("%.2f %%", h);
  Serial.print(F("  P="));
  if (isnan(p)) Serial.print(F("nan")); else Serial.printf("%.2f hPa", p);
  Serial.println();
}

/*
String getTrend(float now, float ref) {
  float d = now - ref;
  if (d > 0.3) return "H";
  if (d < -0.3) return "L";
  return "S";
}
*/


void setupWiFi() {
  WiFi.mode(WIFI_STA);

  auto tryConnect = [](const char* s, const char* p) -> bool {
    Serial.printf("WiFi try: %s\n", s);
    WiFi.begin(s, p);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      yield();
    }
    return WiFi.status() == WL_CONNECTED;
  };

  if (!tryConnect(ssid, password)) {
    Serial.println("Primary WiFi failed, try backup...");
    tryConnect(ssid2, password2);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("WiFi OK");
    display.println(WiFi.localIP());
    display.display();

    timeClient.begin();
    timeClient.update();
  } else {
    Serial.println("WiFi FAIL");

    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
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
         "  background: url('https://ir.ozone.ru/s3/multimedia-6/c1000/6014892378.jpg') no-repeat center center fixed,"
         "              url('https://192.168.1.123/static/010.jpg') no-repeat center center fixed;"
         "  background-size: cover;"
         "}"
         "h1{font-size:1.6rem} .v{font-size:2.2rem} a{color:#0a74da;text-decoration:none}"
         "</style>"
         "<meta http-equiv='refresh' content='10'>"
         "</head><body>");
  s += body;
  s += F("<hr><p> • <a href='/'>Сводка</a> • <a href='/relay/on'>Включить свет</a> • <a href='/relay/off'>Выключить свет</a> • </p>"
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
  server.send(200, "text/html; charset=utf-8", htmlWrap("Температура", "<h1>Температура</h1><p class='v'>"+v+" °C</p>"));
}

void handleHum() {
  String v = isnan(lastHum) ? "—" : String(lastHum, 1);
  server.send(200, "text/html; charset=utf-8", htmlWrap("Влажность", "<h1>Влажность</h1><p class='v'>"+v+" %</p>"));
}

void handlePres() {
  String v = isnan(lastPres) ? "—" : String(lastPres, 1);
  server.send(200, "text/html; charset=utf-8", htmlWrap("Давление", "<h1>Давление</h1><p class='v'>"+v+" hPa</p>"));
}

void handleRelayOn() {
  setRelay(false);  
  server.send(200, "text/html; charset=utf-8", htmlWrap("Реле", "<h1>Реле включено</h1><p><a href=\"/\">Назад</a></p>"));
}

void handleRelayOff() {
  setRelay(true); 
  server.send(200, "text/html; charset=utf-8", htmlWrap("Реле", "<h1>Реле выключено</h1><p><a href=\"/\">Назад</a></p>"));
}

/*
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
*/

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
  String t = F("🌡 <b>Климат:</b>\n");

  t += F("T: ");
  t += isnan(lastTemp) ? "—" : String(lastTemp, 2);
  t += F(" °C\n");

  t += F("H: ");
  t += isnan(lastHum) ? "—" : String(lastHum, 1);
  t += F(" %\n");

  t += F("P: ");
  t += isnan(lastPres) ? "—" : String(lastPres, 1);
  t += F(" hPa");

  return t;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot->messages[i].chat_id;
    String text    = bot->messages[i].text;
    String from    = bot->messages[i].from_name;

    Serial.printf("[TG] msg from %s (%s): %s\n", from.c_str(), chat_id.c_str(), text.c_str());

    if (text == "Включить свет") {

      if (relayState == true) {        // если сейчас ВЫКЛ
        setRelay(false);               // ВКЛ
        lastMotionTime = millis();     // запускаем таймер PIR
      }

      bot->sendMessage(chat_id, "💡 Свет включен", "");
      continue;
    }


    if (text == "Выключить свет") {

      if (relayState == false) {       // если сейчас ВКЛ
        setRelay(true);                // ВЫКЛ
      }

      lastMotionTime = 0;              // сбрасываем таймер
      bot->sendMessage(chat_id, "💤 Свет выключен", "");
      continue;
    }

    if (text == "Климат" || text == "/microclimate" || text == "/start" || text == "/kb") {
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
        "«Климат»",
        "", KB_MICRO, true, false, false);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("Booting..."));

  pinMode(RELAY_PIN, OUTPUT);
//  digitalWrite(RELAY_PIN, LOW);
  setRelay(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP); 

  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), onPirRise, RISING);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonFall, FALLING);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(150000);   

 // display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED not found"));
  }

/*
  i2cScan();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { //Не убирааать! ломается всё...
    Serial.println(F("OLED error"));
    while (true) {
      Serial.println(F("Retry OLED..."));
      delay(3000);
      if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) break;
    }
  }
*/

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AE3000");
  display.setCursor(0, 16);
  display.println("PRTBL");
  display.display();
  delay(400);

  bool bme_ok = bme.begin(0x76);
  if (!bme_ok) {
    Serial.println(F("BME/BMP @0x76 not found, try 0x77..."));
    bme_ok = bme.begin(0x77);
  }
  if (!bme_ok) {
    Serial.println(F("BME280 error"));
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.println("BME280 error");
    display.display();
  } else {
    uint8_t id = bme.sensorID();
    Serial.printf("BME/BMP chipID=0x%02X\n", id);
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
  yield();

  if (g_btnEvent) {
    g_btnEvent = false;
    if (digitalRead(BUTTON_PIN) == LOW) {
      toggleRelay();
      Serial.println("Кнопка");
    }
  }

  if (g_pirEvent) {
    g_pirEvent = false;

    unsigned long now = millis();
    if (now - lastPirTime > PIR_COOLDOWN) {
      lastPirTime = now;
      lastMotionTime = now;

      Serial.println("PIR-ON");

      bool turnedOn = false;

      if (relayState == true) {   
        setRelay(false);
        turnedOn = true;          
      }

      if (turnedOn && bot && WiFi.status() == WL_CONNECTED) {
        bot->sendMessage(chatId5, "Обнаружено движение", "");
      }
    }
  }

  if (relayState == false && lastMotionTime > 0) { 
    if (millis() - lastMotionTime >= LIGHT_TIMEOUT) {
      setRelay(true);   
      lastMotionTime = 0;
    }
  }

  static unsigned long lastScheduleCheck = 0;

  if (useSchedule && millis() - lastScheduleCheck > 30000) {
    lastScheduleCheck = millis();

    bool shouldBeOn = relayBySchedule();

    if (shouldBeOn != relayState) {
      setRelay(!shouldBeOn);

      Serial.printf("[SCHEDULE] %02d:%02d → relay %s\n",
        timeClient.getHours(),
        timeClient.getMinutes(),
        shouldBeOn ? "ON" : "OFF"
      );
    }
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastBotPoll > BOT_POLL_INTERVAL) {
    lastBotPoll = millis();
    int numNew = bot->getUpdates(bot->last_message_received + 1);
    if (numNew > 0) {
      handleNewMessages(numNew);
    }
    yield();
  }

  if (millis() - lastUpdate >= INTERVAL) {
    lastUpdate = millis();

    float temp = bme.readTemperature();
    float hum  = bme.readHumidity();
    float pres = bme.readPressure() / 100.0;

    if (isnan(temp) || isnan(hum) || isnan(pres)) {
      Serial.println(F("[WARN] NaN"));
      yield();
      temp = bme.readTemperature();
      hum  = bme.readHumidity();
      pres = bme.readPressure() / 100.0;
    }

    lastTemp = temp;
    lastHum  = hum;
    lastPres = pres;

    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);

    if (WiFi.status() == WL_CONNECTED) {
      timeClient.update();
      display.printf("%02d:%02d\n",
                     timeClient.getHours(),
                     timeClient.getMinutes());
    } else {
      display.println("--:--");
    }

    display.printf("T: %.1f C\n", lastTemp);
    display.printf("H: %.1f %%\n", lastHum);
    display.printf("P: %.1f hPa\n", lastPres);

    display.display();
  }
  
}
