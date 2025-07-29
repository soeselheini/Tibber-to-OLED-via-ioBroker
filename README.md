# Tibber-to-OLED-via-ioBroker
Tibber hourly tariff to OLED 1.3"  via ioBroker

# Tibber OLED Price Graph Display (ESP8266 + ioBroker)

This project displays hourly electricity prices from Tibber on a compact 128x64 OLED screen, using data fetched via ioBroker. It runs on an ESP8266 (e.g., NodeMCU, Wemos D1 Mini), and updates prices once per hour, showing a simple bar graph and current minimum/maximum values in alternating views.


💡 Features

- 📶 WiFi-enabled (ESP8266)
- 🔌 Pulls Tibber prices via [ioBroker Simple API](https://github.com/ioBroker/ioBroker.simple-api)
- 📊 Graphical bar chart for 24 hours
- 🌙 OLED brightness optimization for 24/7 display (OLED lifespan >5 years)
- 🔄 Automatic switching between graph and info display
- 🌓 Night mode: Dimmed display between 1–6 AM


🛠️ Hardware Requirements

- ESP8266 board (NodeMCU or Wemos D1 Mini recommended)
- SH1106 128x64 OLED Display (I2C)
- WiFi access
- A running [ioBroker](https://www.iobroker.net/) instance with Tibber integration (via `0_userdata.0.Tibber_Tagespreise`)


🔧 Setup

1. Install dependencies (if using Arduino IDE):
   - U8g2 by Oliver Kraus
   - NTPClient
   - ArduinoJson
   - ESP8266WiFi
   - ESP8266HTTPClient

2. Configure your WiFi & ioBroker API URL in the sketch:
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const char* tibber_api_url = "http://192.168.x.x:8087/getPlainValue/0_userdata.0.Tibber_Tagespreise";
