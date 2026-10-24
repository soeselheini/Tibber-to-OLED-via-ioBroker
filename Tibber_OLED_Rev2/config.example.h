#pragma once
// Copy to config.h; never commit config.h.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define TIBBER_TOKEN "YOUR_TIBBER_TOKEN"
#define TIBBER_HOME_ID "YOUR_HOME_ID"
// Germany: daylight saving time handled automatically; change for your location.
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define PRICE_GROSS 0
#define PRICE_NET 1
#define PRICE_ENERGY 2
#define PRICE_MODE PRICE_GROSS
#define VAT_RATE 0.19f
// Supply the verified root certificate for the currently deployed api.tibber.com
// TLS chain. NO setInsecure() fallback; TLS fails closed if this is unset.
#define TIBBER_ROOT_CA_PEM R"PEM(
PASTE_VERIFIED_ROOT_CA_PEM_HERE
)PEM"
