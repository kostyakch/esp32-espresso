#pragma once
#include <Arduino.h>

enum BrewState { IDLE, PREINFUSION, BREW, DECLINE };

static BrewState brewState = IDLE;
static unsigned long stateStart = 0;

static const unsigned long DEFAULT_PREINFUSION_MS = 2000;
static const unsigned long DEFAULT_BREW_MS        = 25000;
static const unsigned long DEFAULT_FINISH_MS     = 5000;

static unsigned long preinfusionMs = DEFAULT_PREINFUSION_MS;
static unsigned long brewMs        = DEFAULT_BREW_MS;
static unsigned long finishMs      = DEFAULT_FINISH_MS;

inline BrewState brewGetState() { return brewState; }
inline const char* brewGetStateName() {
  switch (brewState) {
    case IDLE: return "idle";
    case PREINFUSION: return "preinfusion";
    case BREW: return "brew";
    case DECLINE: return "decline";
  }
  return "idle";
}

inline void brewStart() {
  stateStart = millis();
  if (preinfusionMs > 0)
    brewState = PREINFUSION;
  else
    brewState = BREW;
}

inline void brewStop() {
  brewState = IDLE;
}

inline void brewSetTempirature() {
  brewState = IDLE;
}

inline void brewSetTimes(unsigned long pre, unsigned long brew, unsigned long finish) {
  preinfusionMs = pre;
  brewMs = brew;
  finishMs = finish;
}

inline void brewLoop() {
  if (brewState == IDLE) {
    return;
  }

  unsigned long elapsed = millis() - stateStart;

  switch (brewState) {
    case PREINFUSION:
      if (elapsed >= preinfusionMs) {
        stateStart = millis();
        brewState = BREW;
      }
      break;
    case BREW:
      if (elapsed >= brewMs) {
        stateStart = millis();
        brewState = (finishMs > 0) ? DECLINE : IDLE;
      }
      break;
    case DECLINE:
      if (elapsed >= finishMs) {
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
    case BREW: return brewMs;
    case DECLINE: return finishMs;
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

inline unsigned long brewGetPreinfusionMs() { return preinfusionMs; }
inline unsigned long brewGetBrewMs() { return brewMs; }
inline unsigned long brewGetFinishMs() { return finishMs; }
