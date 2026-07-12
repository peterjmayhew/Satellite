// Satellite GPS Logger
// Hardware:
//   MCU     : ESP32-S3 (Freenove WROOM)
//   GNSS    : SparkFun u-blox NEO-M9N (UART @ 38400)
//   Display : 2.8" ILI9341 320x240 SPI TFT (TFT_eSPI)
//   Storage : SPI microSD (shares the TFT SPI bus)
//   Input   : BOOT button + 3x4 matrix keypad
//   Uplink  : WiFi -> HTTPS POST JSON to a WordPress REST endpoint

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "display.h"
#include "config.h"
#include "sdcard.h"
#include <SD.h>
#include "gps.h"
#include "timesync.h"
#include "keypanel.h"
#include "wifi_uplink.h"

#if USE_UBX
#include "gps_ubx.h"
#else
#include "gps_nmea.h"
#endif

// GPS Serial pins (ESP32 UART1)
#define GPS_RX 16
#define GPS_TX 17
#define GPS_BAUD 38400

// --- Globals ---
HardwareSerial GPSserial(1);
TFT_eSPI tft = TFT_eSPI();

unsigned long lastDisplayUpdate = 0;
const int refreshIntervalSeconds = 5;
const unsigned long refreshIntervalMs = refreshIntervalSeconds * 1000;

// Live GPS data (shared with the parser + screen modules)
float latitude = 0.0;
float longitude = 0.0;
float speedKmph = 0.0;
float heading = 0.0;
float altitudeM = 0.0;   // metres above mean sea level (GGA/UBX)
float hdop = 0.0;        // horizontal dilution of precision
int satellitesSCRN1 = 0;

// Richer fix quality/motion data (populated by the UBX parser; 0 in NMEA mode)
float accH_m = 0.0;      // horizontal position accuracy estimate (m)
float accV_m = 0.0;      // vertical position accuracy estimate (m)
float vspeed_ms = 0.0;   // vertical / climb speed (m/s, positive = up)
float hae_m = 0.0;       // height above WGS84 ellipsoid (m)
float geoidSep_m = 0.0;  // geoid separation = HAE - MSL (m)
float speedAcc_ms = 0.0; // speed accuracy estimate (m/s)
float headAcc_deg = 0.0; // heading accuracy estimate (deg)
float vdop = 0.0;        // vertical dilution of precision
float pdop = 0.0;        // position dilution of precision
int fixType = 0;         // 0 = none, 2 = 2D, 3 = 3D, 5 = time-only
bool sbasUsed = false;   // SBAS/DGNSS differential corrections applied to the fix

// Receiver health / integrity (UBX MON-RF + NAV-STATUS)
int rfJamState = 0;      // 0 = unknown, 1 = ok, 2 = warning, 3 = critical
int rfJamInd = 0;        // CW jamming indicator, 0..255
int rfAgcPct = 0;        // automatic gain control level as a percentage (0..100)
int rfAntStatus = 0;     // 0 = init, 1 = unknown, 2 = ok, 3 = short, 4 = open
int rfNoise = 0;         // MON-RF broadband noise floor (noisePerMS, receiver units)
int spoofState = 0;      // 0 = unknown, 1 = none, 2 = indicated, 3 = multiple
uint32_t ttffMs = 0;     // time to first fix (ms)

// On-chip odometer (UBX NAV-ODO): hardware-filtered ground distance (m)
uint32_t odoDist = 0;    // distance since last reset (this power-on)
uint32_t odoTotal = 0;   // lifetime cumulative distance

// Horizontal error ellipse (from UBX NAV-COV position covariance)
float errMajorM = 0.0;   // semi-major axis 1-sigma std dev (m)
float errMinorM = 0.0;   // semi-minor axis 1-sigma std dev (m)
float errOrientDeg = 0.0;// bearing of the major axis from North (deg, 0..180)

// --- GPS link diagnostics (read by the diagnostic screen) ---
uint32_t diagBytesRx = 0;    // total bytes read from the GPS UART
uint32_t diagNmeaCount = 0;  // '$' NMEA sentence starts seen
uint32_t diagUbxSync = 0;    // UBX B5 62 sync pairs detected
uint32_t diagUbxFrames = 0;  // valid (checksum-OK) UBX frames
uint32_t diagUbxBadCk = 0;   // UBX frames failing checksum
uint32_t diagCfgSends = 0;   // UBX configuration (re)sends
uint32_t diagLastByteMs = 0; // millis() of the last byte received
uint32_t diagNavPvt = 0, diagNavDop = 0, diagNavStatus = 0, diagNavCov = 0, diagNavSat = 0, diagMonRf = 0;
uint8_t diagRing[64];        // last 64 raw bytes read from the GPS UART
uint8_t diagRingIdx = 0;     // next write position in diagRing
bool gpsFix = false;
String timeUTC = "00:00:00";
String dateUTC = "00/00/0000";

// Screen 4 (gauge) rolling state
float currentSpeed = 0;
float currentHeading = 0;
float speedHistory[30];
unsigned long lastUpdate = 0;

// SD logging cadence
static unsigned long lastLogMs = 0;

// --- Function prototypes (screens + helpers implemented in other files) ---
bool checkButton();

void updateDisplay1();
void updateDisplay2();              // constellation / satellites-in-view
void updateDisplay3();
void updateDisplay4();
void updateDisplay5_SkyPlot();
void updateDisplay6_Trip();
void updateDisplay7_SDReadback();
void updateDisplay8_SDFileViewer();
void updateDisplay9_SDBrowser();
void updateDisplayDiag();           // GPS link diagnostics

void tripSample();                  // Screen6: continuous trip integration
void speedSample();                 // Screen4: timed speed-history sampling
static void handleLogging();

// --- Helper: redraw the current screen now ---
// true  = repaint the whole screen incl. static labels (only on screen entry);
// false = update just the dynamic value fields in place (the timed 5 s refresh),
// so the display no longer blanks-and-repaints every cycle (no flicker).
bool g_fullRedraw = true;

static void redrawScreenNow() {
  static int lastScreen = -1;
  g_fullRedraw = (counter != lastScreen);   // full repaint only when the screen actually changed
  lastScreen = counter;
  switch (counter) {
    case 0: updateDisplay1(); break;
    case 1: updateDisplay2(); break;
    case 2: updateDisplay3(); break;
    case 3: updateDisplay4(); break;
    case 4: updateDisplay5_SkyPlot(); break;
    case 5: updateDisplay6_Trip(); break;
    case 6: updateDisplay7_SDReadback(); break;
    case 7: updateDisplay8_SDFileViewer(); break;
    case 8: updateDisplay9_SDBrowser(); break;
    case 9: updateDisplayDiag(); break;
    default: updateDisplay1(); break;
  }
  lastDisplayUpdate = millis();
}

void stage(const char* msg) {
  Serial.println(msg);
  Serial.flush();

  const int footerY = 220;
  const int footerH = 20;
  tft.fillRect(0, footerY, 320, footerH, ILI9341_BLACK);
  tft.setCursor(0, footerY);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.print(msg);
}

static const int SCREEN_COUNT = 10; // counter: 0..9 (9 = GPS link diagnostics)

static void changeScreenBy(int delta) {
  counter += delta;
  if (counter < 0) counter = SCREEN_COUNT - 1;
  if (counter >= SCREEN_COUNT) counter = 0;
  redrawScreenNow();
}

#if !USE_UBX
// Send a raw UBX frame to the GPS (used only to recover a silenced module).
static void gpsSendUbx(uint8_t cls, uint8_t id, const uint8_t* pl, uint16_t n) {
  uint8_t a = 0, b = 0;
  auto upd = [&](uint8_t x){ a += x; b += a; };
  GPSserial.write(0xB5); GPSserial.write(0x62);
  GPSserial.write(cls); upd(cls);
  GPSserial.write(id);  upd(id);
  GPSserial.write((uint8_t)(n & 0xFF)); upd((uint8_t)(n & 0xFF));
  GPSserial.write((uint8_t)(n >> 8));   upd((uint8_t)(n >> 8));
  for (uint16_t i = 0; i < n; i++) { GPSserial.write(pl[i]); upd(pl[i]); }
  GPSserial.write(a); GPSserial.write(b);
}

// UBX-CFG-RST: ask the GPS module to reboot itself (controlled software reset,
// hot start). This reloads its config from defaults, recovering a module a
// prior UBX session left silent - no physical power cycle needed, IF the module
// can still receive on its RX line.
static void gpsSoftResetModule() {
  uint8_t p[4] = { 0x00, 0x00, 0x01, 0x00 }; // navBbrMask=0x0000, resetMode=0x01
  gpsSendUbx(0x06, 0x04, p, 4);
  Serial.println("[reset] sent UBX CFG-RST (software reboot) to GPS");
}

// Diagnostic dual-enable: turn ON both the standard NMEA sentences AND the key
// UBX messages on UART1, disabling nothing. If the module is healthy the raw
// dump then shows NMEA ($G...) and/or UBX (B5 62 ...) so we can see what it
// actually emits. Also recovers a module a prior UBX session left silent.
// (GPS config persists across an ESP reflash - only a power cycle clears it.)
static void gpsDiagEnableAll() {
  // Clean NMEA config: enable the standard NMEA sentences on UART1 and DISABLE
  // the UBX NAV messages, so the module streams pure NMEA (no binary to confuse
  // the NMEA parser). This also un-silences a module a prior UBX session broke.
  const uint8_t nmea[] = { 0x00, 0x04, 0x03, 0x02, 0x05, 0x01 }; // GGA,RMC,GSV,GSA,VTG,GLL
  for (uint8_t i = 0; i < sizeof(nmea); i++) {
    uint8_t p[8] = { 0xF0, nmea[i], 0, 1, 0, 0, 0, 0 }; // rate[UART1] = 1
    gpsSendUbx(0x06, 0x01, p, 8);
    delay(15);
  }
  const uint8_t ubxOff[] = { 0x07, 0x35, 0x04, 0x03, 0x36, 0x01 }; // NAV-PVT/SAT/DOP/STATUS/COV/POSLLH
  for (uint8_t i = 0; i < sizeof(ubxOff); i++) {
    uint8_t p[8] = { 0x01, ubxOff[i], 0, 0, 0, 0, 0, 0 }; // rate 0 = off
    gpsSendUbx(0x06, 0x01, p, 8);
    delay(15);
  }
  Serial.println("[diag] set clean NMEA config on GPS");
}

// Auto-recovery: if no GPS bytes have arrived for a while, try a software reset
// + NMEA re-enable to un-stick the module. Only fires when genuinely silent, so
// it never interferes while data is flowing.
static void handleGpsRecovery() {
  static uint32_t lastTry = 0;
  uint32_t age = (diagLastByteMs == 0) ? millis() : (millis() - diagLastByteMs);
  if (age > 8000 && (millis() - lastTry) > 10000) {
    lastTry = millis();
    Serial.println("[recover] GPS silent - re-init UART + reset module");
    // 1) Re-initialise the ESP UART, clearing any framing-error stall.
    GPSserial.end();
    delay(30);
    GPSserial.setRxBufferSize(2048);
    GPSserial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
    delay(30);
    // 2) Nudge the module back to life.
    gpsSoftResetModule();
    delay(150);
    gpsDiagEnableAll();
  }
}
#endif

// --- Setup ---
static void swWatchdogTask(void*);   // software watchdog (defined after setup)

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=== ESP32 Satellite GPS Logger ===");

  // GPS: enlarge the RX buffer so a blocking WiFi POST can't drop NMEA bytes.
  GPSserial.setRxBufferSize(2048);
  GPSserial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  gpsParserInit(GPSserial);   // pure listen; the module streams NMEA by default

  // SD card — only when logging is enabled (see ENABLE_SD_LOG in config.h).
  // Skipping sdInit when disabled also avoids setup() hanging in SD.begin() if
  // the card is in a bad state, which would otherwise stall boot before the
  // display initialises and leave the TFT blank white.
#if ENABLE_SD_LOG
  sdInit(true);
#endif

  // Display
  tft.begin();
  tft.setRotation(1);

  bootScreenNew();
  delay(500);

  counter = 0;
  lastDisplayUpdate = 0;

  // BOOT button + keypad
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  keypanelInit();

  // WiFi uplink to WordPress (non-fatal if it can't connect)
  wifiUplinkInit();

  // Software watchdog: self-heal from ANY main-loop stall. Everything (GPS, TFT,
  // keypad, WiFi) runs on loopTask, and a blocking call (e.g. a wedged esp_wifi
  // stop/start) would otherwise freeze the whole device forever — the Arduino
  // loopTask is NOT on the task watchdog and TWDT panic is off. This independent
  // core-0 task reboots the board if loop() stops advancing, so a hang becomes a
  // brief auto-recovery instead of a permanent freeze.
  xTaskCreatePinnedToCore(swWatchdogTask, "swwdt", 2560, nullptr, 5, nullptr, 0);

  Serial.println("Setup complete - starting main loop");
}

// Incremented at the top of every loop(); watched by swWatchdogTask.
static volatile uint32_t g_loopBeat = 0;

static void swWatchdogTask(void*) {
  uint32_t last = 0;
  uint8_t  stalls = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t beat = g_loopBeat;
    if (beat == last) {
      // No legitimate loop() operation takes anywhere near this long (the longest,
      // a blocking TLS POST, is bounded to ~6 s), so 20 s of no progress = a hang.
      if (++stalls >= 20) {
        Serial.println("[swwdt] loop() stalled >20s -> rebooting to self-heal");
        Serial.flush();
        ESP.restart();
      }
    } else {
      last = beat;
      stalls = 0;
    }
  }
}

// --- Forward decls ---
static void handleHeartbeat();
static void handleDebug();
static void handleSerialDiag();
static void handleNavigation();
static void handleAutoRefresh();

// --- Loop ---
void loop() {
  g_loopBeat++;   // feed the software watchdog (swWatchdogTask)
  handleHeartbeat();
  handleDebug();
  handleSerialDiag();
  handleNavigation();
  gpsParserProcess(GPSserial);
#if !USE_UBX
  handleGpsRecovery();   // un-stick the module if it goes silent
#endif
  handleLogging();
  tripSample();
  speedSample();
  wifiUplinkLoop();   // POST telemetry to WordPress
  handleAutoRefresh();
}

static void handleHeartbeat() {
  static unsigned long hb = 0;
  if (millis() - hb >= 1000) {
    Serial.println("Heartbeat 1s");
    hb = millis();
  }
}

// Machine-readable diagnostics for the PC-side capture/debug tooling.
// Emits every 2 s: a counters line, a hex dump of the last 64 GPS bytes, and
// the same bytes as ASCII (so NMEA sentences are readable).
static void handleSerialDiag() {
  static unsigned long last = 0;
  if (millis() - last < 2000) return;
  last = millis();

#if USE_UBX
  const char* mode = "UBX";
#else
  const char* mode = "NMEA";
#endif
  uint32_t age = (diagLastByteMs == 0) ? 999999UL : (millis() - diagLastByteMs);

  Serial.printf("###DIAG mode=%s ms=%lu bytesRx=%lu nmea=%lu ubxSync=%lu ubxFrames=%lu badCk=%lu cfgSends=%lu fix=%d type=%d sats=%d age=%lu heap=%lu minheap=%lu maxblk=%lu\n",
    mode, (unsigned long)millis(), (unsigned long)diagBytesRx, (unsigned long)diagNmeaCount,
    (unsigned long)diagUbxSync, (unsigned long)diagUbxFrames, (unsigned long)diagUbxBadCk,
    (unsigned long)diagCfgSends, (int)gpsFix, fixType, satellitesSCRN1, (unsigned long)age,
    (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(), (unsigned long)ESP.getMaxAllocHeap());

  Serial.print("###RAW ");
  for (int i = 0; i < 64; i++) {
    Serial.printf("%02X ", diagRing[(uint8_t)(diagRingIdx + i) & 63]);
  }
  Serial.println();

  Serial.print("###ASC ");
  for (int i = 0; i < 64; i++) {
    uint8_t bb = diagRing[(uint8_t)(diagRingIdx + i) & 63];
    Serial.write((bb >= 32 && bb < 127) ? (char)bb : '.');
  }
  Serial.println();
}

static void handleDebug() {
  static unsigned long lastDbg = 0;
  if (millis() - lastDbg > 5000) {
    Serial.print("gpsFix="); Serial.print(gpsFix);
    Serial.print(" sats="); Serial.print(satellitesSCRN1);
    Serial.print(" sdReady="); Serial.print(sdIsReady());
    Serial.print(" wifi="); Serial.println(wifiUplinkIsConnected());
    lastDbg = millis();
  }
}

static void handleNavigation() {
  // BOOT button -> next screen (checkButton only detects; we own the counter)
  if (checkButton()) {
    changeScreenBy(+1);
  }

  // Keypad: * previous, # next
  int navDelta = 0;
  if (keypanelGetNavDelta(navDelta)) {
    changeScreenBy(navDelta);
  }
}

static void handleLogging() {
#if ENABLE_SD_LOG
  const unsigned long logIntervalMs = 1000;

  if (sdIsReady() && gpsFix && (millis() - lastLogMs >= logIntervalMs)) {
    const String& lastLine = gpsGetLastLine();
    sdLogGPS(lastLine, gpsFix, satellitesSCRN1,
             latitude, longitude, speedKmph, heading, dateUTC, timeUTC);
    lastLogMs = millis();
  }
#else
  (void)lastLogMs;   // SD recording disabled (see ENABLE_SD_LOG in config.h)
#endif
}

static void handleAutoRefresh() {
  if (millis() - lastDisplayUpdate > refreshIntervalMs) {
    redrawScreenNow();
  }
}
