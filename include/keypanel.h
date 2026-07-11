#pragma once
#include <Arduino.h>

// Initialise keypad GPIO
void keypanelInit();

// Get a single debounced key press (returns 0 if none)
char keypanelGetKey();

// Convenience: returns true if it changed screen,
// and sets delta to +1 (next) or -1 (prev)
bool keypanelGetNavDelta(int &delta);

// Optional: change the pins if needed before init
void keypanelSetPins(const int rowPins[4], const int colPins[3]);
