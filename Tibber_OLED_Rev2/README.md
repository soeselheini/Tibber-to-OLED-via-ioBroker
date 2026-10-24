# Tibber OLED – Rev.2.0 (standalone ESP8266)

Standalone Tibber GraphQL access (no ioBroker / MQTT). Hourly prices via `priceInfo(resolution: HOURLY)` displayed on the existing 128x64 SH1106 OLED. Original bar chart, current-hour highlight and alternating min/max display retained. OLED night dimming / longevity routines removed.

> **Development candidate:** Not yet compiled or tested on physical hardware with a live Tibber account. A valid trusted root CA PEM **must** be supplied for HTTPS.

## Hardware / dependencies

ESP8266 (Wemos D1 Mini / NodeMCU), I2C SH1106 128x64. ESP8266 Arduino core, **ArduinoJson 6.x**, U8g2.

## Setup

1. Open `Tibber_OLED_Rev2/Tibber_OLED_Rev2.ino` in Arduino IDE.
2. Copy `config.example.h` to `config.h` in the same folder; enter Wi-Fi SSID, password, Tibber personal API token, and **home ID**. Query available home IDs in the Tibber developer API Explorer: `{ viewer { homes { id appNickname } } }`.
3. Choose `TIMEZONE` as POSIX TZ string (Germany default automatically handles CET/CEST) and `PRICE_MODE`: GROSS / NET / ENERGY.
4. Obtain and verify the issuing trusted root CA for the current `api.tibber.com` TLS chain; paste PEM in `TIBBER_ROOT_CA_PEM`. The sketch refuses an unconfigured CA rather than disabling certificate checks. Chain changes require maintenance.
5. Flash the board; inspect serial log at 115200 baud.

## Optimized API schedule (still Rev.2.0)

- At initial startup, **if today's valid prices are missing**, fetch today's data and any available tomorrow prices.
- Normally **one scheduled request per day between 16:00 and 16:30 local time** (per-device/day stagger): fetch `today` **and** `tomorrow` in a single HTTPS POST. Once tomorrow data is cached, do not request again that day.
- At midnight, **switch the cached tomorrow values to today without HTTP**.
- If no valid current-day data or afternoon tomorrow data is absent, retry with exponential backoff plus random jitter: approximately 10, 20, 40, 80, max 120 minutes. No polling while both relevant data sets are available.
- Data is RAM-only; a restart or power failure requires fresh download. Cached prices from the same day are preserved through transient API failures.
- `today` must contain at least 23 distinct clock-hour values. A transient missing `tomorrow` results in retry scheduling.
- API response contains hourly `startsAt`, `total`, `energy`. Price display uses **ct/kWh**: GROSS=total*100, NET=total/(1+VAT_RATE)*100 (VAT removal approximation), ENERGY=energy*100 (not equivalent to the net retail price).
- On DST transition days 23/25 entries may occur; 25-hour days have a duplicated clock hour that is collapsed. The graph is not an exact DST-day visualization.
- Prices can in rare cases be corrected after initial publication. This once-daily schedule prioritizes low API traffic and may not immediately reflect subsequent revisions.

## Security

BearSSL with configured trust root and certificate validation (not `setInsecure()`); TLS requires valid NTP time. Token sent in Bearer authorization header, not printed. Keep `config.h` and binaries containing secrets private; `config.h` is gitignored.

## Original version

Rev.1 (`tibber_oled_display.ino.ino`) is unchanged on main. Rev.2.0 is maintained in `rev2-direct-tibber`.
