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

// Fully NON-BLOCKING key read: returns a key at most once per physical press
// and NEVER blocks the main loop. The previous version used delay()-based
// debounce plus a `while (scanOnce()==k) delay(5)` wait-for-release; a stuck,
// held, or electrically-noisy key made that loop spin forever (delay() yields,
// so even the task watchdog couldn't catch it) and froze the whole device.
// Here debounce is tracked across calls via millis(), and a still-pressed key
// simply stops producing events until it is seen released — no busy-wait.
char keypanelGetKey() {
  static char reported = 0;       // key already emitted, waiting to see release
  static char cand = 0;           // key currently being debounced
  static uint32_t candSince = 0;  // millis() when cand was first seen

  char k = scanOnce();

  // Nothing pressed (or key released): clear the latch so the next distinct
  // press can be reported. This is the only exit a stuck key never reaches —
  // and because we never loop here, a stuck key just yields no more events.
  if (k == 0) { reported = 0; cand = 0; return 0; }

  // Same key we already reported and haven't seen released yet: no auto-repeat.
  if (k == reported) { cand = 0; return 0; }

  // Debounce a new candidate across successive calls (no delay()).
  if (k != cand) { cand = k; candSince = millis(); return 0; }
  if ((uint32_t)(millis() - candSince) < debounceMs) return 0;

  // Held stable for debounceMs: emit exactly one event, latch until release.
  reported = k;
  cand = 0;
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
