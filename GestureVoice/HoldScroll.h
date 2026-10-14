#pragma once
#include <stdint.h>
#include <cmath>
#include <algorithm>

// 上下保持连续选择，只使用MPU。旋转轴来自补录up/down各001~007训练记录。
// 画圈/右侧翻继续交给原模型；这不是把200帧模型截短后运行。
class HoldScroll {
 public:
 static constexpr float ENTER_DEG=40.f,EXIT_DEG=24.f;
 static constexpr uint32_t HOLD_MS=300,FIRST_REPEAT_MS=650;
 char update(uint32_t now,const int16_t* raw,uint32_t repeatMs=240) {
  justStopped=false;
  const bool gap=haveLast_ && uint32_t(now-last_)>40;
  haveLast_=true;last_=now;
  float g[3]={raw[0]/16384.f,raw[1]/16384.f,raw[2]/16384.f};
  const float norm=length(g);
  const float rate=std::sqrt(float(raw[4])*raw[4]+float(raw[5])*raw[5]+float(raw[6])*raw[6])/131.f;
  if(gap || !std::isfinite(norm) || norm<.75f || norm>1.3f){stop();pending_=false;return 0;}
  for(float& x:g)x/=norm;
  if(!referenceReady){
   if(rate>35){referenceCount_=0;referenceAt_=now;return 0;}
   if(!referenceCount_){for(unsigned k=0;k<3;++k)reference_[k]=g[k];referenceAt_=now;}
   else {
    if(dot(reference_,g)<std::cos(8.f/57.29578f)){referenceCount_=0;return 0;}
    for(unsigned k=0;k<3;++k)reference_[k]=.9f*reference_[k]+.1f*g[k];normalize(reference_);
   }
   ++referenceCount_;
   if(referenceCount_>=30 && uint32_t(now-referenceAt_)>=300)referenceReady=true;
   return 0;
  }
  float cross[3]={reference_[1]*g[2]-reference_[2]*g[1],reference_[2]*g[0]-reference_[0]*g[2],reference_[0]*g[1]-reference_[1]*g[0]};
  const float direction=dot(axis_,cross),crossNorm=length(cross);
  angle=std::atan2(direction,dot(reference_,g))*57.29578f;
  const float coherence=crossNorm>1.e-5f?std::fabs(direction)/crossNorm:0;
  currentCoherence_=coherence;currentRate_=rate;
  if(blocked_){if(std::fabs(angle)<EXIT_DEG && rate<=35)blocked_=false;pending_=false;return 0;}
  if(active_){
   if(std::fabs(angle)<EXIT_DEG || angle*sign_<=0 || coherence<.78f || rate>150){stop();pending_=false;return 0;}
   // 轻微抖动只暂停发出操作，不重新等待首轮650毫秒。
   // 恢复稳定120毫秒后继续；回位、换方向及明显转动仍立即退出。
   if(rate>45){motionPaused_=true;settling_=false;return 0;}
   if(motionPaused_){
    if(!settling_){settling_=true;settleAt_=now;return 0;}
    if(uint32_t(now-settleAt_)<120)return 0;
    motionPaused_=settling_=false;repeated_=true;repeatAt_=now;
   }
   const uint32_t wait=repeated_?repeatMs:FIRST_REPEAT_MS;
   if(uint32_t(now-repeatAt_)>=wait){repeated_=true;repeatAt_=now;return sign_>0?'s':'w';}
   return 0;
  }
  if(std::fabs(angle)<15 && rate<35){
   // 仅在大致原位微调参考，绝不向保持中的倾角追零。
   for(unsigned k=0;k<3;++k)reference_[k]=.995f*reference_[k]+.005f*g[k];normalize(reference_);
  }
  const int sign=angle>=0?1:-1;
  const bool held=std::fabs(angle)>=ENTER_DEG && coherence>=.85f && rate<=35;
  if(!held){pending_=false;return 0;}
  if(!pending_ || sign!=pendingSign_ || std::fabs(angle-pendingAngle_)>5){pending_=true;pendingAt_=now;pendingSign_=sign;pendingAngle_=angle;return 0;}
  if(uint32_t(now-pendingAt_)>=HOLD_MS){active_=true;pending_=false;motionPaused_=settling_=false;sign_=sign;repeated_=false;repeatAt_=now;return sign_>0?'s':'w';}
  return 0;
 }
 // 只有原模型明确判为上下翻，且当前仍沿对应轴保持倾斜，才接续连翻。
 bool continueModel(uint32_t now,char key){
  const int sign=key=='s'?1:key=='w'?-1:0;
  if(!sign || !referenceReady || blocked_ || !haveLast_ || uint32_t(now-last_)>40 ||
     std::fabs(angle)<30.f || angle*sign<=0 || currentCoherence_<.85f || currentRate_>45)return false;
  active_=true;pending_=false;sign_=sign;repeated_=false;repeatAt_=now;
  motionPaused_=settling_=false;justStopped=false;return true;
 }
 void suspend(){motionPaused_=settling_=false;active_=pending_=repeated_=false;haveLast_=false;justStopped=false;blocked_=true;}
 void reset(){suspend();referenceReady=false;referenceCount_=0;angle=0;blocked_=false;}
 bool active()const{return active_;}
 bool referenceReady=false,justStopped=false;
 float angle=0;
 private:
 static float dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
 static float length(const float* a){return std::sqrt(dot(a,a));}
 static void normalize(float* a){float n=length(a);for(unsigned k=0;k<3;++k)a[k]/=n;}
 void stop(){motionPaused_=settling_=false;justStopped=active_;active_=false;repeated_=false;blocked_=true;}
 // 正方向为下压，负方向为上翘；与本次训练数据标签一致。
 const float axis_[3]={.94614074f,-.13719897f,.29324758f};
 float reference_[3]={0,0,1},pendingAngle_=0,currentCoherence_=0,currentRate_=0;
 unsigned referenceCount_=0;
 int sign_=0,pendingSign_=0;
 uint32_t last_=0,referenceAt_=0,pendingAt_=0,repeatAt_=0,settleAt_=0;
 bool haveLast_=false,active_=false,pending_=false,repeated_=false,blocked_=false,motionPaused_=false,settling_=false;
};
