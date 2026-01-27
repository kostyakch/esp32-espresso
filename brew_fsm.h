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
inline const char* brewGetStateName() {
  switch (brewState) {
    case IDLE: return "idle";
    case PREINFUSION: return "preinfusion";
    case PAUSE: return "pause";
    case BREW: return "brew";
  }
  return "idle";
}

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

inline void brewLoop() {
  if (brewState == IDLE) {
    return;
  }

  unsigned long elapsed = millis() - stateStart;

  switch (brewState) {
    case PREINFUSION:
      if (elapsed >= preinfusionMs) {
        brewState = PAUSE;
        stateStart = millis();
      }
      break;
    case PAUSE:
      if (elapsed >= pauseMs) {
        brewState = BREW;
        stateStart = millis();
      }
      break;
    case BREW:
      if (elapsed >= brewMs) {
        brewState = IDLE;
      }
      break;
    default:
      break;
  }
}

inline unsigned long brewGetPhaseTotalMs() {
  switch (brewState) {
    case PREINFUSION: return preinfusionMs;
    case PAUSE: return pauseMs;
    case BREW: return brewMs;
    default: return 0;
  }
}

inline unsigned long brewGetElapsedMs() {
  if (brewState == IDLE) {
    return 0;
  }
  return millis() - stateStart;
}

inline unsigned long brewGetRemainingMs() {
  unsigned long total = brewGetPhaseTotalMs();
  unsigned long elapsed = brewGetElapsedMs();
  return (total > elapsed) ? (total - elapsed) : 0;
}
