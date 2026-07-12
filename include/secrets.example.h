#pragma once
// Copy this file to "secrets.h" and fill in your values.
// secrets.h is git-ignored so your credentials stay out of version control.

// ---- WiFi ----
// Leave WIFI_SSID empty ("") to disable the WiFi uplink entirely.
#define WIFI_SSID       ""
#define WIFI_PASS       ""

// ---- WordPress endpoint ----
// Full URL of the plugin's ingest route, e.g.
//   https://your-site.example/wp-json/satgps/v1/ingest
#define SATGPS_ENDPOINT ""

// Shared secret. Must match the API key set on the WordPress plugin
// settings page (Satellite GPS -> Settings).
#define SATGPS_API_KEY  ""

// Identifier for this receiver (lets you run more than one device).
#define SATGPS_DEVICE_ID "sat-01"

// How often to POST telemetry, in milliseconds.
#define SATGPS_POST_INTERVAL_MS 10000

// ---- Over-the-air (OTA) firmware updates ----
// Password required to push firmware over WiFi (espota --auth=...). STRONGLY
// recommended: leaving it empty allows anyone on your LAN to reflash the device.
#define OTA_PASSWORD ""
