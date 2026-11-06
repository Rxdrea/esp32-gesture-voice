#pragma once
#include <stdint.h>
#include "BrainProtocol.h"

namespace eeg_decision {
constexpr uint8_t START_THRESHOLD=70, STOP_THRESHOLD=50, CONFIRM_PACKETS=2;
constexpr uint32_t MAX_PACKET_GAP_MS=3000;
// 先使用双阈值＋连续两包，不额外增加平滑延迟；需要时可开启轻度平滑。
constexpr bool ENABLE_SMOOTHING=false;
constexpr float SMOOTH_ALPHA=.3f;

class Decision {
 uint8_t state_=0,pending_=0,count_=0;
 bool haveLast_=false,filterReady_=false;
 uint32_t lastMs_=0;
 float value_=0;
public:
 void invalidate(){pending_=count_=0;haveLast_=filterReady_=false;}
 bool accept(uint32_t now,uint8_t attention,bool valid) {
  if(!valid || attention>100){invalidate();return false;}
  if(haveLast_ && uint32_t(now-lastMs_)>MAX_PACKET_GAP_MS)invalidate();
  haveLast_=true;lastMs_=now;
  if(!filterReady_){value_=attention;filterReady_=true;}
  else value_=ENABLE_SMOOTHING?(1.f-SMOOTH_ALPHA)*value_+SMOOTH_ALPHA*attention:attention;
  uint8_t wanted=value_>START_THRESHOLD?uint8_t(EegCommand::Start):
                 value_<STOP_THRESHOLD?uint8_t(EegCommand::Stop):0;
  // 中间区间、回到原状态、短暂回落都会取消尚未确认的切换。
  if(!wanted || wanted==state_){pending_=count_=0;return false;}
  if(wanted!=pending_){pending_=wanted;count_=1;}else if(count_<CONFIRM_PACKETS)++count_;
  if(count_<CONFIRM_PACKETS)return false;
  state_=wanted;pending_=count_=0;return true;
 }
 uint8_t state()const{return state_;}
 uint8_t pending()const{return pending_;}
 uint8_t count()const{return count_;}
 float value()const{return value_;}
};
}
