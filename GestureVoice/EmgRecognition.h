#pragma once
#include <CheezsEMG.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include "FusionCore.h"
const uint8_t EMG_PIN=34, DETECT_PIN=2;
const uint8_t LED_PINS[]={5,18,19};
const int LED_THRESHOLDS[]={6,9,12};
const uint32_t EMG_SAMPLE_RATE=500;
const TickType_t EMG_TICKS=pdMS_TO_TICKS(2);
// 32 点浮点包络（沿用 1.1.2 GestureCore.h）
class FloatEnvelope {
 public:
  float add(float filtered) {
    sum_ -= history_[index_];
    history_[index_] = fabsf(filtered);
    sum_ += history_[index_];
    index_ = (index_ + 1) % 32;
    return (float)(2.0 * fmax(0.0, sum_) / 32.0);
  }
 private:
  float history_[32] = {};
  double sum_ = 0;
  unsigned index_ = 0;
};

// 佩戴过滤（沿用 1.1.2 GestureCore.h）：接触信号抖动过滤
class WearFilter {
 public:
  bool update(uint32_t now, bool high) {
    if (!worn_) {
      uncertain_ = false; lowPending_ = false;
      if (!high) highPending_ = false;
      else {
        if (!highPending_) { highAt_ = now; highPending_ = true; }
        if (uint32_t(now - highAt_) >= 40) { worn_ = true; highPending_ = false; }
      }
      return worn_;
    }
    if (!high) {
      if (!uncertain_) { uncertain_ = true; uncertainAt_ = now; }
      highPending_ = false;
      if (!lowPending_) { lowPending_ = true; lowAt_ = now; }
    } else {
      lowPending_ = false;
      if (!uncertain_) return true;
      if (!highPending_) { highPending_ = true; highAt_ = now; }
      if (uint32_t(now - highAt_) >= 40) { uncertain_ = false; highPending_ = false; return true; }
    }
    if ((lowPending_ && uint32_t(now - lowAt_) >= 100) ||
        (uncertain_ && uint32_t(now - uncertainAt_) >= 200)) {
      worn_ = uncertain_ = lowPending_ = highPending_ = false;
    }
    return worn_;
  }
  bool uncertain() const { return uncertain_; }
 private:
  bool worn_ = false, uncertain_ = false, highPending_ = false, lowPending_ = false;
  uint32_t uncertainAt_ = 0, highAt_ = 0, lowAt_ = 0;
};

struct EmgView {
  float newEnv = 0;
  bool worn = false;
  bool uncertain = false;
  bool warmed = false;
  uint32_t timeUs = 0;
  uint32_t sequence = 0;
};
CheezsEMG sEMG(EMG_PIN, DETECT_PIN, EMG_SAMPLE_RATE);
portMUX_TYPE emgMux = portMUX_INITIALIZER_UNLOCKED;
EmgView sharedEmg;

EmgView emgSnapshot() {
  portENTER_CRITICAL(&emgMux);
  EmgView copy = sharedEmg;
  portEXIT_CRITICAL(&emgMux);
  return copy;
}

// 500Hz 肌电采样任务（跑在核心1，与 1.1.2 一致）
void emgSampleTask(void* parameter) {
  (void)parameter;
  TickType_t wake = xTaskGetTickCount();
  const uint32_t boot = millis();
  EmgView value;
  FloatEnvelope envelope;
  WearFilter contact;
  for (;;) {
    sEMG.processSignal();
    const float filtered = sEMG.getFilteredSignal();
    value.newEnv = envelope.add(filtered);
    value.worn = contact.update(millis(), sEMG.getDetectSignal() == HIGH);
    const int oldEnv = sEMG.getEnvelopeSignal();
    const bool warmed = uint32_t(millis() - boot) >= 1500;
    // 与 1.1.2 一致：预热完成且检测为已佩戴后，按 6/9/12 三档亮灯。
    // CSV 仍记录浮点新包络，亮灯档位不能代替数据分析。
    for (size_t i = 0; i < 3; ++i) {
      digitalWrite(LED_PINS[i], warmed && value.worn &&
                   oldEnv >= LED_THRESHOLDS[i] ? HIGH : LOW);
    }
    value.warmed = warmed;
    value.uncertain = contact.uncertain();
    value.timeUs = micros();
    ++value.sequence;
    portENTER_CRITICAL(&emgMux);
    sharedEmg = value;
    portEXIT_CRITICAL(&emgMux);
    vTaskDelayUntil(&wake, EMG_TICKS);
  }
}



// 高频采样保持不变；动作决策交给可在电脑逐帧回放的同一份核心。
fusion::Core gripCore;
bool neutralReady=false,gripArmed=false,resultGrip=false;
unsigned gripCount=0,debugActive=0,debugResets=0,debugLastFault=0;
float displayEnvelope=0,debugRate=0,debugAngle=0,emgModelGripProbability=0;
uint32_t debugStepMs=0,debugDisplayUs=0,lastEmgSequence=0;
const char* gripHint="等待传感数据";
const char* emgStatus="等待数据";
void cancelGrip(uint32_t now){gripCore.inhibit(now);}
bool updateGrip(uint32_t now,const int16_t* raw) {
 static uint32_t lastStep=0;if(lastStep)debugStepMs=max(debugStepMs,uint32_t(now-lastStep));lastStep=now;
 EmgView e=emgSnapshot();displayEnvelope=e.newEnv;
 bool fresh=e.sequence!=lastEmgSequence;lastEmgSequence=e.sequence;
 fusion_features::Sample a={raw[0]/16384.f,raw[1]/16384.f,raw[2]/16384.f,raw[4]/131.f,raw[5]/131.f,raw[6]/131.f,e.newEnv};
 bool valid=e.warmed && fresh && uint32_t(micros()-e.timeUs)<=20000 && std::isfinite(e.newEnv);
 bool fired=gripCore.update(now,a,valid,e.worn&&!e.uncertain);
 neutralReady=gripCore.neutralReady;gripArmed=gripCore.armed;gripCount=gripCore.count;
 debugActive=gripCore.above10;debugResets=gripCore.resets;debugLastFault=gripCore.fault;
 debugRate=gripCore.rate;debugAngle=gripCore.angle;gripHint=gripCore.hint;
 emgModelGripProbability=gripCore.gripProbability;
 emgStatus=gripCore.motionProbability>=.5f?"手腕运动":gripCore.gripProbability>=.7f?"握拳候选":gripCore.gripProbability<=.3f?"放松候选":"正在分辨";
 if(fired)resultConf=gripCore.gripProbability;
 return fired;
}
void printGripDiagnostic() {
 static uint32_t lastLog=0;uint32_t now=millis();if(uint32_t(now-lastLog)<500)return;lastLog=now;
 EmgView e=emgSnapshot();char line[180];
 int n=snprintf(line,sizeof(line),"DBG E=%.1f W=%u N=%u A=%u C=%u G=%.2f M=%.2f R=%u F=%u S=%lu O=%lu\n",
  e.newEnv,e.worn&&!e.uncertain,neutralReady,gripArmed,gripCount,gripCore.gripProbability,gripCore.motionProbability,
  debugResets,debugLastFault,(unsigned long)debugStepMs,(unsigned long)(debugDisplayUs/1000));
 if(n>0 && n<int(sizeof(line)) && Serial.availableForWrite()>=n){Serial.write((const uint8_t*)line,n);debugStepMs=debugDisplayUs=0;}
}
