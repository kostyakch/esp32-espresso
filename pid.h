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
  }

  float compute(float input) {
    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    if (dt < minDt || dt > maxDt) {
      lastTime = now;
      return lastOutput;
    }

    float error = setpoint - input;

    float derivative = (error - lastError) / dt;

    // When at or above setpoint, zero integral so we don't overshoot or hold temp above setpoint
    if (error <= 0) integral = 0;
    else {
      // Conditional integration when below setpoint to reduce windup at output limits
      float nextIntegral = integral + error * dt;
      float output = Kp * error + Ki * nextIntegral + Kd * derivative;
      if (!(output > outMax && error > 0))
        integral = constrain(nextIntegral, -iClamp, iClamp);
    }
    float output = Kp * error + Ki * integral + Kd * derivative;

    output = constrain(output, outMin, outMax);

    lastError = error;
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
  float lastOutput = 0;
  unsigned long lastTime = 0;

  float outMin, outMax;
  float iClamp = 50;   // защита от windup
  float minDt = 0.05;  // 20 Hz max PID update rate
  float maxDt = 2.0;
};
