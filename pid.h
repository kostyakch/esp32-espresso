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

  void reset() {
    integral = 0;
    lastError = 0;
    lastTime = millis();
  }

  float compute(float input) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    if (dt <= 0 || dt > 2.0) {
      lastTime = now;
      return lastOutput;
    }

    float error = setpoint - input;

    // --- Integral with anti-windup
    integral += error * dt;
    integral = constrain(integral, -iClamp, iClamp);

    float derivative = (error - lastError) / dt;

    float output =
      Kp * error +
      Ki * integral +
      Kd * derivative;

    output = constrain(output, outMin, outMax);

    lastError = error;
    lastTime = now;
    lastOutput = output;

    return output;
  }

  void setIntegralClamp(float clamp) { iClamp = clamp; }

private:
  float Kp, Ki, Kd;
  float setpoint = 0;

  float integral = 0;
  float lastError = 0;
  float lastOutput = 0;
  unsigned long lastTime = 0;

  float outMin, outMax;
  float iClamp = 50;   // защита от windup
};
