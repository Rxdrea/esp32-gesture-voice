#pragma once
#include "Arduino.h"
class CheezsEMG {
  uint8_t input_,detect_;
 public:
  CheezsEMG(uint8_t input,uint8_t detect,uint32_t):input_(input),detect_(detect) {}
  void begin() { pinMode(input_,INPUT); pinMode(detect_,INPUT); }
  void processSignal() { raw_=analogRead(input_); }
  int getRawSignal() const { return raw_; }
  float getFilteredSignal() const { return 1.0f; }
  int getEnvelopeSignal() const { return 2; }
  int getDetectSignal() const { return digitalRead(detect_); }
 private:
  int raw_=512;
};
