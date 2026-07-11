#include <Arduino.h>
#include "config.h"

#if !USE_UBX

#include "gps_nmea.h"
#include "gps.h"
#include "timesync.h"
#include "gnss_extra.h"

// Globals defined in main.cpp
extern float latitude, longitude;
extern float speedKmph, heading;
extern float altitudeM, hdop;
extern bool gpsFix;
extern String timeUTC, dateUTC;
extern int satellitesSCRN1;

// Link diagnostics (defined in main.cpp)
extern uint32_t diagBytesRx, diagNmeaCount, diagLastByteMs;
extern uint8_t diagRing[64];
extern uint8_t diagRingIdx;

// ---------------- Internal state ----------------
static String lineBuf;

// Fix timeout (prevents instant flicker when a single epoch is missed).
// Must exceed the worst-case blocking WiFi POST (~a few seconds) so an uplink
// stall can't drop a perfectly valid fix.
static uint32_t lastFixMs = 0;
static const uint32_t FIX_TIMEOUT_MS = 10000;

// GSV epoch detection: the receiver emits all constellations' GSV sentences
// back-to-back once per epoch, then pauses until the next epoch. We reset the
// sky list on the first GSV after that pause so every constellation from a
// single epoch is kept (the old code reset on every talker's msg 1, which
// wiped all but the last constellation). Assumes NMEA output <= a few Hz.
static uint32_t lastGsvMs = 0;
static const uint32_t GSV_EPOCH_GAP_MS = 400;

// -------- Helpers --------
static float convertToDecimalDegrees(float raw, char direction) {
  int degrees = int(raw / 100);
  float minutes = raw - (degrees * 100);
  float decimalDegrees = degrees + (minutes / 60.0f);
  if (direction == 'S' || direction == 'W') decimalDegrees = -decimalDegrees;
  return decimalDegrees;
}

static void splitNMEA(const String &nmea, String* fields, int maxFields) {
  int startIdx = nmea.indexOf(',') + 1;
  for (int i = 0; i < maxFields; i++) {
    int endIdx = nmea.indexOf(',', startIdx);
    if (endIdx == -1) { fields[i] = nmea.substring(startIdx); break; }
    fields[i] = nmea.substring(startIdx, endIdx);
    startIdx = endIdx + 1;
  }
}

static String formatTime(const String &t) {
  if (t.length() < 6) return "00:00:00";
  return t.substring(0,2) + ":" + t.substring(2,4) + ":" + t.substring(4,6);
}

static String formatDate(const String &d) {
  if (d.length() < 6) return "00/00/0000";
  return d.substring(0,2) + "/" + d.substring(2,4) + "/20" + d.substring(4,6);
}

// -------- Sentence handlers --------
static void handleRMC(const String &nmea) {
  String f[12];
  splitNMEA(nmea, f, 12);

  // f[0]=time, f[1]=status, f[2]=lat, f[3]=N/S, f[4]=lon, f[5]=E/W,
  // f[6]=knots, f[7]=course, f[8]=date
  if (f[1] == "A") {
    float latRaw = f[2].toFloat();
    char  latDir = f[3].length() ? f[3][0] : 'N';
    float lonRaw = f[4].toFloat();
    char  lonDir = f[5].length() ? f[5][0] : 'E';

    latitude  = convertToDecimalDegrees(latRaw, latDir);
    longitude = convertToDecimalDegrees(lonRaw, lonDir);

    speedKmph = f[6].toFloat() * 1.852f; // knots -> km/h
    heading   = f[7].toFloat();

    timeUTC = formatTime(f[0]);
    dateUTC = formatDate(f[8]);

    gpsFix = true;
    lastFixMs = millis();

    syncTimeFromGPS(dateUTC, timeUTC);
  }
  // Do NOT set gpsFix=false here; the timeout below handles loss of fix.
}

static void handleGGA(const String &nmea) {
  String f[15];
  splitNMEA(nmea, f, 15);

  // f[5]=fix quality, f[6]=sats used, f[7]=HDOP, f[8]=altitude (m)
  int fixQuality  = f[5].toInt();
  satellitesSCRN1 = f[6].toInt();
  hdop            = f[7].toFloat();
  altitudeM       = f[8].toFloat();

  if (fixQuality > 0) {
    gpsFix = true;
    lastFixMs = millis();
  }
}

static void handleGSV(const String &nmea) {
  // New epoch? Reset the accumulated sky list once, then add every
  // constellation that follows within this burst.
  uint32_t now = millis();
  if (now - lastGsvMs > GSV_EPOCH_GAP_MS) {
    skyReset();
  }
  lastGsvMs = now;

  char talker = '?';
  if (nmea.startsWith("$GPGSV")) talker = 'P';
  else if (nmea.startsWith("$GLGSV")) talker = 'L';
  else if (nmea.startsWith("$GAGSV")) talker = 'A';
  else if (nmea.startsWith("$GBGSV")) talker = 'B';
  else if (nmea.startsWith("$GQGSV")) talker = 'Q'; // QZSS

  String parts[20];
  int partIndex = 0;
  int start = 0;
  for (int i = 0; i < nmea.length(); i++) {
    if (nmea[i] == ',' || nmea[i] == '*') {
      parts[partIndex++] = nmea.substring(start, i);
      start = i + 1;
      if (partIndex >= 20) break;
    }
  }

  // parts[4..] repeat in groups of 4: prn, elev, az, snr
  for (int i = 4; i + 3 < partIndex; i += 4) {
    int prn  = parts[i].toInt();
    int elev = parts[i + 1].toInt();
    int az   = parts[i + 2].toInt();
    int snr  = parts[i + 3].length() ? parts[i + 3].toInt() : -1;

    if (prn > 0) skyAddSat(prn, elev, az, snr, talker);
  }
}

// -------- Public API --------
void gpsParserInit(HardwareSerial &gps) {
  (void)gps;
  lineBuf.reserve(128);
  lastFixMs = millis();
}

void gpsParserProcess(HardwareSerial &gps) {
  const uint32_t start = millis();

  while (gps.available()) {
    char c = (char)gps.read();
    diagBytesRx++;
    diagLastByteMs = millis();
    diagRing[diagRingIdx] = (uint8_t)c; diagRingIdx = (diagRingIdx + 1) & 63;
    if (c == '$') diagNmeaCount++;

    if (c == '\n') {
      lineBuf.trim();
      if (lineBuf.length()) {
        if (lineBuf.startsWith("$GNRMC") || lineBuf.startsWith("$GPRMC")) {
          gpsUpdateLine(lineBuf);   // expose last position sentence to screens/logger
          handleRMC(lineBuf);
        } else if (lineBuf.startsWith("$GNGGA") || lineBuf.startsWith("$GPGGA")) {
          gpsUpdateLine(lineBuf);
          handleGGA(lineBuf);
        } else if (lineBuf.indexOf("GSV") > 0) {
          handleGSV(lineBuf);
        }
      }
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
      // Guard: a valid NMEA sentence is <= 82 chars. If we exceed that with no
      // newline (binary/garbage on the line), drop it so the String can't grow
      // unbounded and exhaust the heap.
      if (lineBuf.length() > 100) lineBuf = "";
    }

    // Keep the UI responsive: don't hog the loop on a burst of data
    if (millis() - start >= 4) break;
  }

  // Drop the fix only if it has gone stale
  if (millis() - lastFixMs > FIX_TIMEOUT_MS) {
    gpsFix = false;
  }
}

#endif
