#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "wifi_uplink.h"
#include "config.h"
#include "gnss_extra.h"
#include "secrets.h"

// Live GPS data (defined in main.cpp)
extern float latitude, longitude, speedKmph, heading;
extern float altitudeM, hdop;
extern float accH_m, accV_m, vspeed_ms, hae_m, geoidSep_m, speedAcc_ms, headAcc_deg, vdop, pdop;
extern int satellitesSCRN1;
extern int fixType;
extern bool gpsFix;
extern bool sbasUsed;
extern int rfJamState, rfJamInd, rfAgcPct, rfAntStatus, rfNoise, spoofState;
extern uint32_t ttffMs;
extern uint32_t odoDist, odoTotal;
extern float errMajorM, errMinorM, errOrientDeg;
extern String timeUTC, dateUTC;

static bool g_enabled = false;
static uint32_t g_lastPost = 0;
static uint32_t g_lastReconnect = 0;
static const uint32_t WIFI_RETRY_MS = 20000;   // gentle reconnect cadence (no auth flood)

// Build an ISO-8601 UTC timestamp from the GPS date/time strings.
// dateUTC = "DD/MM/YYYY", timeUTC = "HH:MM:SS".
static String isoTimestamp() {
  if (dateUTC.length() >= 10 && timeUTC.length() >= 8 && dateUTC != "00/00/0000") {
    return dateUTC.substring(6, 10) + "-" + dateUTC.substring(3, 5) + "-" +
           dateUTC.substring(0, 2) + "T" + timeUTC + "Z";
  }
  return "";
}

static String buildJson() {
  String s;
  s.reserve(2048);
  s += "{";
  s += "\"device\":\""; s += SATGPS_DEVICE_ID; s += "\",";
  s += "\"fw\":\"";     s += VERSION;          s += "\",";
  s += "\"ts\":\"";     s += isoTimestamp();   s += "\",";
  s += "\"date\":\"";   s += dateUTC;          s += "\",";
  s += "\"time\":\"";   s += timeUTC;          s += "\",";
  s += "\"fix\":";          s += (gpsFix ? "true" : "false"); s += ",";
  s += "\"sats_used\":";    s += satellitesSCRN1;   s += ",";
  s += "\"sats_in_view\":"; s += skySatCount;       s += ",";
  s += "\"lat\":";          s += String(latitude, 6);   s += ",";
  s += "\"lon\":";          s += String(longitude, 6);  s += ",";
  s += "\"alt_m\":";        s += String(altitudeM, 1);  s += ",";
  s += "\"speed_kmh\":";    s += String(speedKmph, 2);  s += ",";
  s += "\"heading_deg\":";  s += String(heading, 1);    s += ",";
  s += "\"hdop\":";         s += String(hdop, 2);       s += ",";
  // Richer fix quality / motion (populated in UBX mode; 0 in NMEA mode)
  s += "\"fix_type\":";     s += fixType;               s += ",";
  s += "\"acc_h_m\":";      s += String(accH_m, 2);     s += ",";
  s += "\"acc_v_m\":";      s += String(accV_m, 2);     s += ",";
  s += "\"vspeed_ms\":";    s += String(vspeed_ms, 2);  s += ",";
  s += "\"hae_m\":";        s += String(hae_m, 1);      s += ",";
  s += "\"geoid_m\":";      s += String(geoidSep_m, 1); s += ",";
  s += "\"vdop\":";         s += String(vdop, 2);       s += ",";
  s += "\"pdop\":";         s += String(pdop, 2);       s += ",";
  s += "\"speed_acc_ms\":"; s += String(speedAcc_ms, 2);s += ",";
  s += "\"head_acc_deg\":"; s += String(headAcc_deg, 1);s += ",";
  s += "\"sbas\":";         s += (sbasUsed ? "true" : "false"); s += ",";
  // Receiver health / integrity (UBX MON-RF + NAV-STATUS)
  s += "\"jam_state\":";    s += rfJamState;            s += ",";
  s += "\"jam_ind\":";      s += rfJamInd;              s += ",";
  s += "\"agc_pct\":";      s += rfAgcPct;              s += ",";
  s += "\"noise\":";        s += rfNoise;               s += ",";
  s += "\"ant_status\":";   s += rfAntStatus;           s += ",";
  s += "\"spoof_state\":";  s += spoofState;            s += ",";
  s += "\"ttff_ms\":";      s += String(ttffMs);        s += ",";
  s += "\"odo_m\":";        s += String(odoDist);       s += ",";
  s += "\"odo_total_m\":";  s += String(odoTotal);      s += ",";
  s += "\"err_major_m\":";  s += String(errMajorM, 2);  s += ",";
  s += "\"err_minor_m\":";  s += String(errMinorM, 2);  s += ",";
  s += "\"err_orient_deg\":"; s += String(errOrientDeg, 1); s += ",";
  s += "\"uptime_ms\":";    s += String(millis());      s += ",";
  s += "\"sats\":[";
  for (int i = 0; i < skySatCount; i++) {
    if (i) s += ",";
    s += "{\"c\":\""; s += String(skySats[i].talker); s += "\",";
    s += "\"prn\":";  s += skySats[i].prn;  s += ",";
    s += "\"el\":";   s += skySats[i].elev; s += ",";
    s += "\"az\":";   s += skySats[i].az;   s += ",";
    s += "\"snr\":";  s += skySats[i].snr;  s += ",";
    s += "\"u\":";    s += (skySats[i].used ? 1 : 0); s += "}";
  }
  s += "]}";
  return s;
}

static void doPost() {
  String url = SATGPS_ENDPOINT;
  if (url.length() == 0) return;

  String payload = buildJson();
  int code = -1;

  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);

  if (url.startsWith("https")) {
    WiFiClientSecure client;
    client.setInsecure();  // skip cert validation (typical for embedded devices)
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-API-Key", SATGPS_API_KEY);
      code = http.POST(payload);
      http.end();
    }
  } else {
    WiFiClient client;
    if (http.begin(client, url)) {
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-API-Key", SATGPS_API_KEY);
      code = http.POST(payload);
      http.end();
    }
  }

  Serial.printf("[uplink] POST -> %d (%u bytes)\n", code, (unsigned)payload.length());
}

void wifiUplinkInit() {
  if (strlen(WIFI_SSID) == 0) {
    g_enabled = false;
    Serial.println("[uplink] disabled (no WiFi configured in secrets.h)");
    return;
  }
  g_enabled = true;
  WiFi.persistent(false);   // don't rewrite NVS on every begin() attempt
  WiFi.mode(WIFI_STA);

  // Log the STA MAC once so it can be found (or checked against an access-control
  // list) in the router's admin page without any guesswork.
  Serial.printf("[uplink] STA MAC %s\n", WiFi.macAddress().c_str());
  // Print the exact reason on every disconnect (15=wrong password/handshake
  // timeout, 201=AP not found, 2/4=auth issues, 3/8=deauth/assoc-leave, etc.).
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[uplink] WiFi disconnected, reason=%u\n", info.wifi_sta_disconnected.reason);
    }
  });
  WiFi.setSleep(false);  // keep radio fully awake — modem sleep can make some APs
                         // deauth during the auth handshake (seen as reason=2).
  // Disable the core auto-reconnect: when the AP is refusing auth (reason=2) it
  // retries ~every 1-2 s, i.e. a flood of failed authentications. Many routers
  // treat that as a brute-force attack and temporarily blacklist the device's
  // MAC, which turns a transient problem into a persistent one. We instead retry
  // GENTLY from wifiUplinkLoop() (once per WIFI_RETRY_MS), which reconnects fine
  // when the AP is available but never floods it. (Hang-safety: the retry uses a
  // LIGHT WiFi.disconnect(), never disconnect(true), plus the software watchdog.)
  WiFi.setAutoReconnect(false);

  // NOTE: no WiFi.scanNetworks() here — the long-working build did not scan, and a
  // scan immediately before begin() leaves the radio mid-scan/idle in a way that
  // can make the following association fail. Connect straight away instead.
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[uplink] WiFi connecting to \"%s\"...\n", WIFI_SSID);
}

void wifiUplinkLoop() {
  if (!g_enabled) return;

  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    // Gentle, fixed-interval retry (not the core's ~2 s auto-reconnect storm), so a
    // rejecting AP is never flooded with failed auths. Reconnects within one
    // interval once the router side is healthy.
    if (now - g_lastReconnect >= WIFI_RETRY_MS) {
      g_lastReconnect = now;
      Serial.printf("[uplink] WiFi status=%d, retrying...\n", (int)WiFi.status());
      // Explicit credentials every time (robust regardless of persistent()/NVS),
      // with a LIGHT disconnect() only — never disconnect(true), which does the
      // blocking esp_wifi_stop()/start() radio teardown that could wedge the loop.
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }

  if (now - g_lastPost >= (uint32_t)SATGPS_POST_INTERVAL_MS) {
    g_lastPost = now;
    doPost();
  }
}

bool wifiUplinkIsConnected() {
  return g_enabled && (WiFi.status() == WL_CONNECTED);
}
