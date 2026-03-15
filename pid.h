#pragma once
#include <Arduino.h>

class PIDController {
public:
  PIDController(float kp, float ki, float kd,
                float outMin = 0, float outMax = 100)
    : Kp(kp), Ki(ki), Kd(kd),
      outMin(outMin), outMax(outMax) {}

  void setSetpoint(float sp) { setpoint = sp; }
  float getSetpoint() const { return setpoint; }
  float getKp() const { return Kp; }
  float getKi() const { return Ki; }
  float getKd() const { return Kd; }

  void reset() {
    integral = 0;
    lastError = 0;
    lastTime = millis();
    haveLastInput = false;
  }

  float compute(float input) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    if (dt < minDt)
      return lastOutput;  /* не обновляем lastTime — dt накопит 500 ms и PID пересчитается */
    if (dt > maxDt) {
      lastTime = now;
      return lastOutput;
    }

    float error = setpoint - input;

    // Derivative on measurement: -Kd * d(input)/dt — brakes when temp rises quickly (reduces overshoot)
    float derivative = 0;
    if (haveLastInput)
      derivative = -(input - lastInput) / dt;

    // When at or above setpoint, zero integral so we don't overshoot or hold temp above setpoint
    if (error <= 0) integral = 0;
    else {
      // Anti-windup (clamping): don't accumulate integral when output is saturated
      float nextIntegral = integral + error * dt;
      float output = Kp * error + Ki * nextIntegral + Kd * derivative;
      if (output > outMax && error > 0) {
        // At max output, don't increase integral
      } else if (output < outMin && error < 0) {
        // At min output, don't decrease integral (symmetric clamping)
      } else {
        integral = constrain(nextIntegral, -iClamp, iClamp);
      }
    }
    float output = Kp * error + Ki * integral + Kd * derivative;

    output = constrain(output, outMin, outMax);

    lastError = error;
    lastInput = input;
    haveLastInput = true;
    lastTime = now;
    lastOutput = output;

    return output;
  }

  void setIntegralClamp(float clamp) { iClamp = clamp; }
  void setDtLimits(float minSeconds, float maxSeconds) {
    minDt = minSeconds;
    maxDt = maxSeconds;
  }
  void setTunings(float kp, float ki, float kd) {
    Kp = kp;
    Ki = ki;
    Kd = kd;
  }

private:
  float Kp, Ki, Kd;
  float setpoint = 0;

  float integral = 0;
  float lastError = 0;
  float lastInput = 0;
  bool haveLastInput = false;
  float lastOutput = 0;
  unsigned long lastTime = 0;

  float outMin, outMax;
  float iClamp = 50;   // integral clamp (anti-windup)
  float minDt = 0.5;   // PID cycle 200-1000 ms (variant B: limit update rate here)
  float maxDt = 2.0;
};
