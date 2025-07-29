#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>
#include <stdlib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <math.h>
#include <ArduinoJson.h>

// --- WiFi Configuration ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Tibber API via ioBroker Simple API ---
const char* tibber_api_url = "http://192.168.x.x:8087/getPlainValue/0_userdata.0.Tibber_Tagespreise";

// --- Network and Time Setup ---
WiFiClient espClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // UTC+2

// --- OLED Display ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- Data Buffers ---
#define MAX_JSON_LENGTH 1024
char jsonRawBuffer[MAX_JSON_LENGTH + 1];

float hourlyValues[24];
float minValue = 9999.0;
float maxValue = 0.0;
int parsedValueCount = 0;

bool dataReceived = false;
#define MIN_BAR_PIXEL_HEIGHT 6

int currentHourIndex = -1;
int lastFetchedHour = -1;

enum DisplayMode { GRAPH_MODE, INFO_MODE };
DisplayMode currentDisplayMode = GRAPH_MODE;
unsigned long lastDisplaySwitchTime = 0;
const unsigned long GRAPH_DISPLAY_DURATION = 10000;
const unsigned long INFO_DISPLAY_DURATION = 2000;

// --- Function Declarations ---
void setup_wifi();
void fetchTibberData();
void parseJsonData(const char* jsonString);
void updateDisplay();
void drawMinMaxInfo();
void adjustContrast();

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 8, "Connecting WiFi...");
  u8g2.sendBuffer();

  setup_wifi();

  timeClient.begin();
  timeClient.update();
  currentHourIndex = timeClient.getHours();

  memset(jsonRawBuffer, 0, sizeof(jsonRawBuffer));

  u8g2.clearBuffer();
  u8g2.drawStr(0, 8, "OLED Ready");
  u8g2.drawStr(0, 16, "WiFi OK");
  u8g2.drawStr(0, 24, "Fetching data...");
  u8g2.sendBuffer();
  delay(1000);

  fetchTibberData();
  lastFetchedHour = currentHourIndex;

  lastDisplaySwitchTime = millis(); 
}

void loop() {
  timeClient.update();
  int newHour = timeClient.getHours();

  adjustContrast();

  if (newHour != lastFetchedHour) {
    fetchTibberData();
    lastFetchedHour = newHour;
  }

  if (newHour != currentHourIndex) {
    currentHourIndex = newHour;
  }

  unsigned long currentTime = millis();
  if (currentDisplayMode == GRAPH_MODE) {
    if (currentTime - lastDisplaySwitchTime >= GRAPH_DISPLAY_DURATION) {
      currentDisplayMode = INFO_MODE;
      lastDisplaySwitchTime = currentTime;
    }
  } else {
    if (currentTime - lastDisplaySwitchTime >= INFO_DISPLAY_DURATION) {
      currentDisplayMode = GRAPH_MODE;
      lastDisplaySwitchTime = currentTime;
    }
  }

  updateDisplay();
  delay(100);
}

void setup_wifi() {
  delay(10);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    u8g2.drawStr(0 + (retries % 20) * 6, 16, ".");
    u8g2.sendBuffer();
    retries++;
  }

  u8g2.clearBuffer();
  if (WiFi.status() == WL_CONNECTED) {
    String ip_address = WiFi.localIP().toString();
    long rssi = WiFi.RSSI();
    u8g2.drawStr(0, 8, "WiFi Connected");
    u8g2.drawStr(0, 16, ip_address.c_str());
    char rssi_str[20];
    sprintf(rssi_str, "Signal: %ld dBm", rssi);
    u8g2.drawStr(0, 24, rssi_str);
  } else {
    u8g2.drawStr(0, 8, "WiFi Failed");
    u8g2.drawStr(0, 16, "Check SSID/Password");
    while (true) {
      delay(1000);
    }
  }
  u8g2.sendBuffer();
  delay(2000);
}

void fetchTibberData() {
  HTTPClient http;
  http.begin(espClient, tibber_api_url);
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
      String payload = http.getString();
      String cleanPayload = payload;

      if (cleanPayload.startsWith("\"") && cleanPayload.endsWith("\"") && cleanPayload.length() >= 2) {
        cleanPayload = cleanPayload.substring(1, cleanPayload.length() - 1);
      }
      cleanPayload.replace("\\n", "");
      cleanPayload.replace("\\\"", "\"");

      unsigned int charsToCopy = min((unsigned int)cleanPayload.length(), (unsigned int)MAX_JSON_LENGTH);
      strncpy(jsonRawBuffer, cleanPayload.c_str(), charsToCopy);
      jsonRawBuffer[charsToCopy] = '\0';

      parseJsonData(jsonRawBuffer);
    }
  } else {
    dataReceived = false;
    parsedValueCount = 0;
  }

  http.end();
}

void parseJsonData(const char* jsonString) {
  StaticJsonDocument<MAX_JSON_LENGTH> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    dataReceived = false;
    parsedValueCount = 0;
    return;
  }

  JsonObject prices = doc.as<JsonObject>(); 

  if (!prices.isNull()) {
    minValue = 9999.0;
    maxValue = 0.0;
    parsedValueCount = 0;

    for (int i = 0; i < 24; i++) {
      hourlyValues[i] = 0.0;
    }

    for (JsonPair kv : prices) {
      int hour = atoi(kv.key().c_str());
      float price = kv.value().as<float>();

      if (hour >= 0 && hour < 24) {
        hourlyValues[hour] = price;
        if (price < minValue) minValue = price;
        if (price > maxValue) maxValue = price;
      }
    }

    parsedValueCount = 24;
    dataReceived = true;
  } else {
    dataReceived = false;
    parsedValueCount = 0;
  }
}

void updateDisplay() {
  u8g2.clearBuffer();

  if (currentDisplayMode == INFO_MODE) {
    drawMinMaxInfo();
    return;
  }

  if (!dataReceived || parsedValueCount == 0) {
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.setCursor(0, 32);
    u8g2.print("Waiting for data...");
    u8g2.sendBuffer();
    return;
  }

  if (currentHourIndex >= 0 && currentHourIndex < parsedValueCount) {
    u8g2.setFont(u8g2_font_5x7_tf);
    char priceBuffer[10];
    sprintf(priceBuffer, "%.2f", hourlyValues[currentHourIndex]);
    u8g2.setCursor(0, 7);
    u8g2.print(priceBuffer);
  }

  int displayWidth = u8g2.getDisplayWidth();
  int displayHeight = 64;
  int graphTop = 8;
  int graphBottom = displayHeight - 1;
  int graphHeight = graphBottom - graphTop + 1;
  if (graphHeight <= 0) graphHeight = 1;

  float valueRange = maxValue - minValue;
  if (valueRange == 0) valueRange = 0.001;
  float effectiveGraphHeight = graphHeight - MIN_BAR_PIXEL_HEIGHT;
  if (effectiveGraphHeight <= 0) effectiveGraphHeight = 1;
  int barWidth = 5;

  for (int i = 0; i < parsedValueCount; i++) {
    int x = (i * barWidth) + 1;
    if (i >= 6) x += 1;
    if (i >= 12) x += 1;
    if (i >= 18) x += 1;
    if (x + barWidth > displayWidth) break;

    float barHeightF = ((hourlyValues[i] - minValue) * effectiveGraphHeight / valueRange) + MIN_BAR_PIXEL_HEIGHT;
    int barHeight = round(barHeightF);
    if (barHeight > graphHeight) barHeight = graphHeight;
    if (barHeight < MIN_BAR_PIXEL_HEIGHT) barHeight = MIN_BAR_PIXEL_HEIGHT;
    int y = graphBottom - barHeight + 1;
    if (y < graphTop) y = graphTop;

    if (i == currentHourIndex) {
      if ((millis() / 500) % 2 == 0) {
        u8g2.drawBox(x, y, barWidth, barHeight);
      } else {
        u8g2.drawFrame(x, y, barWidth, barHeight);
      }
    } else {
      u8g2.drawFrame(x, y, barWidth, barHeight);
    }
  }

  u8g2.sendBuffer();
}

void drawMinMaxInfo() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB18_tf);  // Large, readable

  char minBuffer[16];
  char maxBuffer[16];
  sprintf(minBuffer, "Low %.2f", minValue);
  sprintf(maxBuffer, "High %.2f", maxValue);

  u8g2_uint_t minWidth = u8g2.getStrWidth(minBuffer);
  u8g2_uint_t maxWidth = u8g2.getStrWidth(maxBuffer);

  u8g2.setCursor((128 - minWidth) / 2, 30);
  u8g2.print(minBuffer);

  u8g2.setCursor((128 - maxWidth) / 2, 60);
  u8g2.print(maxBuffer);

  u8g2.sendBuffer();
}

void adjustContrast() {
  int hourNow = timeClient.getHours();
  if (hourNow >= 1 && hourNow < 6) {
    u8g2.setContrast(16);   // Night: very dim
  } else {
    u8g2.setContrast(128);  // Day: 50% brightness for OLED longevity
  }
}
