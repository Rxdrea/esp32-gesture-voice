#pragma once
#include "FreeRTOS.h"
#include <stdexcept>
namespace host { struct SamplingStopped {}; static bool stopSampler=false; static void (*sampleTaskFn)(void*)=nullptr; static std::function<void()> onSampleDelay; }
inline TickType_t xTaskGetTickCount() { return 0; }
inline void vTaskDelayUntil(TickType_t*,TickType_t) {
  if(host::onSampleDelay) host::onSampleDelay();
  if(host::stopSampler) throw host::SamplingStopped();
}
// Record, but never asynchronously run, the infinite hardware sampler.
inline int xTaskCreatePinnedToCore(void (*fn)(void*),const char*,unsigned,void*,unsigned,void*,int) { host::sampleTaskFn=fn; return pdPASS; }
