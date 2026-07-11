#pragma once
// src/display.cpp
#include <TFT_eSPI.h>

// Declare the global tft object
extern TFT_eSPI tft;

void bootScreenNew();
void updateDisplay9_SDBrowser();