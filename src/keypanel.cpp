#include "keypanel.h"

// 3x4 keypad (7 wires): D E F G H J K
// Assumption: D,E,F,G are rows and H,J,K are columns.
static const int ROWS = 4;
static const int COLS = 3;

// Default example GPIOs (CHANGE if they clash with TFT/SD/GPS)
static int rowPins[ROWS] = {2, 3, 4, 6};     // D E F G
static int colPins[COLS] = {7, 15, 14};      // H J K

static const char keymap[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

static uint16_t debounceMs = 25;

// Drive one row LOW at a time, read columns (with pullups)
static char scanOnce() {
  for (int r = 0; r < ROWS; r++) {
    for (int rr = 0; rr < ROWS; rr++) {
      digitalWrite(rowPins[rr], (rr == r) ? LOW : HIGH);
    }
    delayMicroseconds(5);

    for (int c = 0; c < COLS; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        return keymap[r][c];
      }
    }
  }
  return 0;
}

void keypanelSetPins(const int rp[4], const int cp[3]) {
  for (int i = 0; i < ROWS; i++) rowPins[i] = rp[i];
  for (int i = 0; i < COLS; i++) colPins[i] = cp[i];
}

void keypanelInit() {
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH); // idle HIGH
  }
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
}

char keypanelGetKey() {
  char k = scanOnce();
  if (!k) return 0;

  // Debounce
  delay(debounceMs);
  if (scanOnce() != k) return 0;

  // Wait for release so one press = one event
  while (scanOnce() == k) {
    delay(5);
  }
  return k;
}

bool keypanelGetNavDelta(int &delta) {
  delta = 0;
  char k = keypanelGetKey();
  if (!k) return false;

  if (k == '#') { delta = +1; return true; }
  if (k == '*') { delta = -1; return true; }
  return false;
}
