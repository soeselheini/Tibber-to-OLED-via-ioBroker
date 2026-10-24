# Changelog

## Rev.2.0 — 2026-10-24 (development candidate)
- Direct Tibber GraphQL API over HTTPS; token-based Bearer authentication and home ID.
- Hourly price data; configurable gross, VAT-excluded approximation or energy component.
- Configurable Wi-Fi credentials and POSIX time zone with automatic daylight-saving transition.
- Removed ioBroker dependency, fixed UTC+2 offset, OLED night/contrast preservation logic.
- Retained hourly OLED bar chart and alternating min/max screens.
- **Optimized polling within Rev.2.0**: immediate startup query only when missing data, once daily staggered between 16:00 and 16:30 local, query both today/tomorrow, local midnight swap, exponential-backoff retries with jitter on missing data (10–120 min).
- Cache held in RAM; no flash-wear risk. Requires valid CA PEM; HTTPS certificate checks remain mandatory.
- Known limitations: RAM cache lost at restart, DST duplicate hour collapsed, firmware build and hardware test pending.

## Rev.1
- Original ioBroker Simple API based hourly price graph.
