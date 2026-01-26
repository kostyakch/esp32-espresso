#pragma once
#include <Arduino.h>

enum BrewState { IDLE, PREINFUSION, PAUSE, BREW };

static BrewState brewState = IDLE;
static unsigned long stateStart = 0;

static const unsigned long DEFAULT_PREINFUSION_MS = 2000;
static const unsigned long DEFAULT_PAUSE_MS       = 4000;
static const unsigned long DEFAULT_BREW_MS        = 25000;

static unsigned long preinfusionMs = DEFAULT_PREINFUSION_MS;
static unsigned long pauseMs       = DEFAULT_PAUSE_MS;
static unsigned long brewMs        = DEFAULT_BREW_MS;

inline BrewState brewGetState() { return brewState; }

inline void brewStart() {
  brewState = PREINFUSION;
  stateStart = millis();
}

inline void brewStop() {
  brewState = IDLE;
}

inline void brewSetTempirature() {
  brewState = IDLE;
}

inline void brewSetTimes(unsigned long pre, unsigned long pause, unsigned long brew) {
  preinfusionMs = pre;
  pauseMs = pause;
  brewMs = brew;
}
