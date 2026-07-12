#include "config.h"
#include "gps.h"

const int BUTTON_PIN = 0;                   // BOOT button (active-low)
const int DEBOUNCE_DELAY = 50;              // ms
const unsigned long updateInterval = 1000;  // ms (used by gauge/history refresh)
const int BATTERY_PIN = 0;                  // GPIO for battery ADC (0 = disabled/stubbed)
int counter = 0;                            // Current screen index (0..SCREEN_COUNT-1)
const char* const VERSION = "1.10";         // FIX hang: non-blocking keypad + software watchdog self-heal + non-teardown WiFi retry w/ backoff

// Logging mode for the SD logger.
//   GPS_LOG_RAW_NMEA | GPS_LOG_CSV | GPS_LOG_BOTH | GPS_LOG_NONE
GpsLogMode gpsLogMode = GPS_LOG_BOTH;
