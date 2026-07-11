#include <Arduino.h>
#include "sdcard.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include "gps.h"

static bool g_sdReady = false;
static SPIClass sdSPI(HSPI);   // if HSPI doesn't compile, try FSPI

static inline void sdBusLock() {
  // Ensure TFT doesn't drive the bus while we talk to SD
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(2);
}

static inline void sdBusUnlock() {
  digitalWrite(SD_CS, HIGH);
  // Don't force TFT_CS low here; TFT_eSPI will manage it.
}

bool sdInit(bool runSelfTest) {
  sdBusLock();

  // Separate SPI controller instance for SD (prevents stomping TFT SPI state)
  sdSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, SD_CS);
  delay(2);

  g_sdReady = SD.begin(SD_CS, sdSPI, 400000);
  if (!g_sdReady) {
    sdBusUnlock();
    return false;
  }

  if (!runSelfTest) {
    sdBusUnlock();
    return true;
  }

  // Simple self-test: create/append file
  File w = SD.open("/sd_test.txt", FILE_APPEND);
  if (w) {
    w.println("sdInit self-test OK");
    w.close();
  }

  sdBusUnlock();
  return true;
}

bool sdIsReady() {
  return g_sdReady;
}

bool sdExists(const char* path) {
  if (!g_sdReady || !path || path[0] != '/') return false;
  sdBusLock();
  bool ok = SD.exists(path);
  sdBusUnlock();
  return ok;
}

bool sdWriteText(const char* path, const char* text, bool append) {
  if (!g_sdReady) return false;
  if (!path || path[0] != '/') return false;
  if (!text) text = "";

  sdBusLock();

  File f = SD.open(path, append ? FILE_APPEND : FILE_WRITE);
  if (!f) {
    sdBusUnlock();
    return false;
  }

  f.print(text);
  // ensure newline termination for logs
  if (text[0] != '\0' && text[strlen(text) - 1] != '\n') f.print('\n');

  f.close();
  sdBusUnlock();
  return true;
}

bool sdReadFileToString(const char* path, String& out, size_t maxBytes) {
  out = "";
  if (!g_sdReady) return false;
  if (!path || path[0] != '/') return false;

  sdBusLock();

  File f = SD.open(path, FILE_READ);
  if (!f) {
    sdBusUnlock();
    return false;
  }

  out.reserve((maxBytes < 4096) ? maxBytes : 4096);
  size_t n = 0;
  while (f.available() && n < maxBytes) {
    out += (char)f.read();
    n++;
  }
  f.close();

  sdBusUnlock();
  return true;
}

// Read last non-empty line without loading whole file
bool sdReadLastNonEmptyLine(const char* path, String& outLine) {
  outLine = "";
  if (!g_sdReady) return false;
  if (!path || path[0] != '/') return false;

  sdBusLock();

  File f = SD.open(path, FILE_READ);
  if (!f) {
    sdBusUnlock();
    return false;
  }

  size_t sz = f.size();
  if (sz == 0) {
    f.close();
    sdBusUnlock();
    return false;
  }

  long pos = (long)sz - 1;

  // skip trailing whitespace/newlines
  while (pos >= 0) {
    f.seek(pos);
    char c = (char)f.read();
    if (c != '\n' && c != '\r' && c != ' ' && c != '\t') break;
    pos--;
  }
  if (pos < 0) {
    f.close();
    sdBusUnlock();
    return false;
  }

  String rev;
  rev.reserve(200);

  while (pos >= 0) {
    f.seek(pos);
    char c = (char)f.read();
    if (c == '\n') break;
    if (c != '\r') rev += c;
    pos--;
    if (rev.length() > 400) break; // safety cap
  }

  // reverse into outLine
  outLine.reserve(rev.length());
  for (int i = (int)rev.length() - 1; i >= 0; --i) outLine += rev[i];
  outLine.trim();

  f.close();
  sdBusUnlock();

  return outLine.length() > 0;
}
bool sdForEachLine(const char* path,
                   SdLineSink sink,
                   void* user,
                   size_t maxLines,
                   size_t maxLineLen) {
  if (!sdIsReady()) return false;
  if (!path || path[0] != '/') return false;
  if (!sink) return false;

  sdBusLock();

  File f = SD.open(path, FILE_READ);
  if (!f) {
    sdBusUnlock();
    return false;
  }

  // Line buffer (stack). Keep it modest.
  size_t bufLen = (maxLineLen < 16) ? 16 : maxLineLen;
  if (bufLen > 512) bufLen = 512; // hard cap
  char buf[512];
  size_t idx = 0;
  size_t lines = 0;

  while (f.available() && lines < maxLines) {
    int ch = f.read();
    if (ch < 0) break;

    if (ch == '\r') continue;

    if (ch == '\n') {
      buf[idx] = '\0';
      sink(buf, user);
      lines++;
      idx = 0;
      continue;
    }

    if (idx < bufLen - 1) {
      buf[idx++] = (char)ch;
    }
  }

  // flush last partial line
  if (idx > 0 && lines < maxLines) {
    buf[idx] = '\0';
    sink(buf, user);
  }

  f.close();
  sdBusUnlock();
  return true;
}

// Bus-locked root directory listing. Skips sub-directories.
int sdListRoot(SdFileInfo* out, int maxFiles) {
  if (!sdIsReady() || !out || maxFiles <= 0) return 0;

  sdBusLock();

  File root = SD.open("/");
  if (!root) {
    sdBusUnlock();
    return 0;
  }

  int count = 0;
  while (count < maxFiles) {
    File entry = root.openNextFile();
    if (!entry) break;

    const char* name = entry.name();
    if (name && !entry.isDirectory()) {
      strncpy(out[count].name, name, sizeof(out[count].name) - 1);
      out[count].name[sizeof(out[count].name) - 1] = '\0';
      out[count].size = entry.size();
      out[count].mtime = (uint32_t)entry.getLastWrite();
      count++;
    }
    entry.close();
  }

  root.close();
  sdBusUnlock();
  return count;
}



// GPS logger

void sdLogGPS(
  const String& nmeaLine,
  bool fix,
  int sats,
  float lat,
  float lon,
  float speedKmph,
  float heading,
  const String& dateUTC,
  const String& timeUTC
) {
  if (!sdIsReady()) return;

  /* ---------- RAW NMEA ---------- */
  // Skip when there is no raw line (e.g. UBX mode has no NMEA sentence to log).
  if ((gpsLogMode == GPS_LOG_RAW_NMEA || gpsLogMode == GPS_LOG_BOTH) && nmeaLine.length() > 0) {
    const char* rawPath = "/gps_raw.txt";
    sdWriteText(rawPath, nmeaLine.c_str(), true);
  }

  /* ---------- CSV ---------- */
  if (gpsLogMode == GPS_LOG_CSV || gpsLogMode == GPS_LOG_BOTH) {
    const char* csvPath = "/gps_log.csv";

    // create header once
    if (!sdExists(csvPath)) {
      sdWriteText(csvPath,
        "ms,dateUTC,timeUTC,fix,sats,lat,lon,speedKmph,headingDeg",
        false
      );
    }

    char row[180];
    snprintf(row, sizeof(row),
      "%lu,%s,%s,%d,%d,%.6f,%.6f,%.2f,%.1f",
      millis(),
      dateUTC.c_str(),
      timeUTC.c_str(),
      fix ? 1 : 0,
      sats,
      lat,
      lon,
      speedKmph,
      heading
    );

    sdWriteText(csvPath, row, true);
  }
}
