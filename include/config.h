#ifndef CONFIG_H
#define CONFIG_H

extern const int BUTTON_PIN;
extern const int DEBOUNCE_DELAY;
extern const unsigned long updateInterval;
extern const int BATTERY_PIN;
extern int counter;               // Current screen index
extern const char* const VERSION; // Software version

// --- Display (TFT_eSPI) pin map (documented here; TFT_eSPI reads its own User_Setup) ---
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10  // Chip select control pin
#define TFT_DC    8  // Data Command control pin
#define TFT_RST   9  // Reset pin

// --- SD card (shares the SPI bus with the TFT) ---
#define SD_CS     5

// Local SD-card recording. OFF by default: every fix is already stored server-side
// in WordPress, so the 1 Hz SD writes were a redundant current draw (a contributor
// to the power sag that disrupted the GPS). Set to 1 and reflash to re-enable
// logging to the SD card.
#define ENABLE_SD_LOG 0

// Small helper: print a one-line status message to the TFT footer + Serial
void stage(const char* msg);

#endif
