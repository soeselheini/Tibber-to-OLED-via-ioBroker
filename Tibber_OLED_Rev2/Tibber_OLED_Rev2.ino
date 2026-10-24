// Tibber OLED Rev.2.0 - standalone ESP8266, hourly Tibber prices
// ArduinoJson 6.x, ESP8266 Arduino core, U8g2 SH1106 128x64
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <time.h>
#include <math.h>
#include "config.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
static const char *API = "https://api.tibber.com/v1-beta/gql";
static const uint32_t RETRY_FIRST_MS = 10UL * 60UL * 1000UL;
static const uint32_t RETRY_MAX_MS = 120UL * 60UL * 1000UL;
float prices[2][24] = {};
bool present[2][24] = {};
int dayKey[2] = {-1, -1};  // local YYYY*1000 + day-of-year
int observedDay = -1; // track rollover separately, even if fetching fails
bool showInfo = false;
unsigned long displayTimer = 0;
unsigned long lastTry = 0;
uint32_t retryDelay = 0; // first startup fetch should be immediate
uint32_t nextBackoff = RETRY_FIRST_MS;
bool retryMode = true;
char message[24] = "Starting...";

void setMessage(const char *s) {
  strncpy(message, s, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  Serial.println(message);
}
int keyOf(const tm &t) { return (t.tm_year + 1900) * 1000 + t.tm_yday; }
bool getClock(tm &t) {
  time_t now = time(nullptr);
  return now > 1700000000 && localtime_r(&now, &t);
}
int nextDayKey(const tm &t) {
  tm copy = t; copy.tm_mday++; copy.tm_hour = 12; copy.tm_isdst = -1;
  time_t epoch = mktime(&copy);
  tm *res = localtime(&epoch);
  return res ? keyOf(*res) : -1;
}
float priceValue(JsonVariantConst row) {
  float total = row["total"] | NAN;
  if (PRICE_MODE == PRICE_NET) return total / (1.0f + VAT_RATE);
  if (PRICE_MODE == PRICE_ENERGY) return row["energy"] | NAN;
  return total;
}
bool usable(int slot) {
  int n = 0;
  for (int i=0; i<24; ++i) n += present[slot][i] ? 1 : 0;
  return n >= 23;
}
void rollDays(const tm &now) {
  int today = keyOf(now);
  if (observedDay == today) return; // do not reset retry timer on every loop
  observedDay = today;
  if (dayKey[0] == today) return;
  if (dayKey[1] == today) {
    memcpy(prices[0], prices[1], sizeof(prices[0]));
    memcpy(present[0], present[1], sizeof(present[0]));
    dayKey[0] = dayKey[1];
  } else {
    memset(present[0], 0, sizeof(present[0]));
    dayKey[0] = -1;
  }
  memset(present[1], 0, sizeof(present[1]));
  dayKey[1] = -1;
  retryMode = !usable(0);
  retryDelay = 0; // fetch missing new-day data without waiting
  nextBackoff = RETRY_FIRST_MS;
}
bool needsFetch(const tm &now) {
  if (!usable(0)) return true;
  // Stagger the daily request across 16:00-16:30 local (changes per day/device).
  uint32_t seed = ESP.getChipId() ^ ((uint32_t)now.tm_yday * 2654435761UL);
  int offsetMin = seed % 31;
  bool afternoon = now.tm_hour > 16 || (now.tm_hour == 16 && now.tm_min >= offsetMin);
  if (afternoon && dayKey[1] != nextDayKey(now)) return true;
  return false;
}
bool fetchTibber(const tm &now) {
  if (WiFi.status() != WL_CONNECTED) { setMessage("No WiFi"); return false; }
  if (strlen(TIBBER_TOKEN) < 8 || strlen(TIBBER_HOME_ID) < 8) {
    setMessage("Set token / home"); return false;
  }
  if (!strstr(TIBBER_ROOT_CA_PEM, "BEGIN CERTIFICATE")) {
    setMessage("Set verified TLS CA"); return false;
  }
  // Protect GraphQL string literal from invalid untrusted input.
  String id(TIBBER_HOME_ID);
  if (id.indexOf('"') >= 0 || id.indexOf('\\') >= 0) {
    setMessage("Invalid home ID"); return false;
  }
  BearSSL::WiFiClientSecure client;
  BearSSL::X509List ca(TIBBER_ROOT_CA_PEM);
  client.setTrustAnchors(&ca);
  client.setTimeout(15000);
  HTTPClient http;
  // Avoid HTTP/1.1 chunked transfer, which cannot be parsed directly from getStream().
  http.useHTTP10(true);
  http.setTimeout(15000);
  if (!http.begin(client, API)) { setMessage("TLS init failed"); return false; }
  http.addHeader("Authorization", String("Bearer ") + TIBBER_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "Tibber-OLED-Rev2/2.0");
  String gql = String("{viewer{home(id:\"") + id +
      "\"){currentSubscription{priceInfo(resolution:HOURLY){today{startsAt total energy} tomorrow{startsAt total energy}}}}}}";
  DynamicJsonDocument request(700);
  request["query"] = gql;
  String body; serializeJson(request, body);
  int httpCode = http.POST(body);
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Tibber status %d\n", httpCode);
    http.end(); setMessage("Tibber HTTP error"); return false;
  }
  // Stream and filter JSON instead of copying whole reply to a String.
  DynamicJsonDocument filter(512);
  JsonObject rows = filter["data"]["viewer"]["home"]["currentSubscription"]["priceInfo"].to<JsonObject>();
  for (const char* name : {"today", "tomorrow"}) {
    rows[name][0]["startsAt"] = true;
    rows[name][0]["total"] = true;
    rows[name][0]["energy"] = true;
  }
  filter["errors"][0]["message"] = true;
  DynamicJsonDocument response(12288);
  auto error = deserializeJson(response, http.getStream(),
      DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(16));
  http.end();
  if (error) { Serial.println(error.c_str()); setMessage("JSON error"); return false; }
  if (!response["errors"].isNull()) { setMessage("GraphQL error"); return false; }

  float tempPrices[2][24] = {};
  bool tempPresent[2][24] = {};
  int tempKeys[2] = {keyOf(now), nextDayKey(now)};
  JsonVariantConst info = response["data"]["viewer"]["home"]["currentSubscription"]["priceInfo"];
  bool accepted[2] = {false, false};
  for (int slot = 0; slot < 2; ++slot) {
    JsonArrayConst array = info[slot == 0 ? "today" : "tomorrow"].as<JsonArrayConst>();
    if (array.isNull()) continue;
    int count = 0;
    for (JsonVariantConst item : array) {
      const char* timestamp = item["startsAt"] | "";
      if (strlen(timestamp) < 13) continue;
      int hour = (timestamp[11]-'0')*10 + timestamp[12]-'0';
      float value = priceValue(item);
      if (hour < 0 || hour > 23 || !isfinite(value)) continue;
      // On 25-hour DST days the repeated local hour is collapsed.
      if (!tempPresent[slot][hour]) {
        tempPresent[slot][hour] = true;
        tempPrices[slot][hour] = value * 100.0f; // EUR/kWh -> ct/kWh
        ++count;
      }
    }
    accepted[slot] = count >= 23;
  }
  // Never overwrite good cached data with missing/incomplete response.
  if (!accepted[0]) { setMessage("Today incomplete"); return false; }
  for (int slot=0; slot<2; ++slot) if (accepted[slot]) {
    memcpy(prices[slot], tempPrices[slot], sizeof(prices[slot]));
    memcpy(present[slot], tempPresent[slot], sizeof(present[slot]));
    dayKey[slot] = tempKeys[slot];
  }
  setMessage(accepted[1] ? "Today + tomorrow OK" : "Today OK; retry");
  return accepted[1] || now.tm_hour < 16;
}
void draw() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  tm now;
  bool timeOK = getClock(now);
  if (!usable(0)) {
    oled.drawStr(0, 18, "Tibber OLED Rev.2.0");
    oled.drawStr(0, 34, message);
    oled.sendBuffer(); return;
  }
  float lo = INFINITY, hi = -INFINITY;
  for (int i=0;i<24;++i) if (present[0][i]) {
    lo = min(lo,prices[0][i]); hi = max(hi,prices[0][i]);
  }
  if (showInfo) {
    char buf[24];
    oled.setFont(u8g2_font_helvB12_tf);
    snprintf(buf,sizeof(buf),"Low %.2f",lo); oled.drawStr(0,25,buf);
    snprintf(buf,sizeof(buf),"High %.2f",hi); oled.drawStr(0,54,buf);
  } else {
    char buf[25];
    int hour=timeOK ? now.tm_hour : -1;
    if (hour >= 0 && present[0][hour])
      snprintf(buf,sizeof(buf),"%02d:00 %.2f ct/kWh",hour,prices[0][hour]);
    else snprintf(buf,sizeof(buf),"Hourly price ct/kWh");
    oled.drawStr(0,8,buf);
    float range=max(hi-lo,0.001f);
    for(int i=0;i<24;++i) {
      if (!present[0][i]) continue;
      int x=i*5+1+(i>=6)+(i>=12)+(i>=18);
      int height=6+(int)lroundf((prices[0][i]-lo)*46.0f/range);
      height=constrain(height,6,53);
      if (i==hour) oled.drawBox(x,64-height,5,height);
      else oled.drawFrame(x,64-height,5,height);
    }
  }
  oled.sendBuffer();
}
void setup() {
  Serial.begin(115200);
  oled.begin();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(TIMEZONE, "pool.ntp.org", "time.cloudflare.com");
  randomSeed(micros() ^ ESP.getChipId());
  setMessage("WiFi / NTP...");
}
void loop() {
  tm now;
  if (getClock(now)) {
    rollDays(now);
    if (needsFetch(now)) {
      if (!retryMode || retryDelay == 0 || (uint32_t)(millis()-lastTry) >= retryDelay) {
        lastTry=millis();
        bool ok=fetchTibber(now);
        retryMode=!ok;
        if (!ok) {
          // Tibber requires exponential backoff with jitter, capped at 120 min.
          uint32_t jitter = (uint32_t)random(0L, (long)(nextBackoff/5UL + 1UL));
          retryDelay = nextBackoff + jitter;
          if (retryDelay > RETRY_MAX_MS) retryDelay = RETRY_MAX_MS;
          if (nextBackoff < RETRY_MAX_MS / 2UL) nextBackoff *= 2UL;
          else nextBackoff = RETRY_MAX_MS;
        } else {
          retryDelay=0;
          nextBackoff=RETRY_FIRST_MS;
        }
      }
    } else {
      retryMode=false;
      retryDelay=0;
      nextBackoff=RETRY_FIRST_MS;
    }
  }
  if ((uint32_t)(millis()-displayTimer) >= 12000UL) {
    showInfo=!showInfo; displayTimer=millis();
  }
  draw();
  delay(500);
}
