#pragma once
#include "FusionFeatures.h"
#include "MotionGuardModel.h"
// 完全独立于肌电、佩戴和握拳状态，保证五种手腕动作只参考MPU。
class WristMotionGate {
 public:
 void update(uint32_t now,const int16_t* raw) {
  if(haveLast_ && uint32_t(now-last_)>40){count_=index_=0;probability=0;ready_=false;positiveCount_=0;}
  haveLast_=true;last_=now;
  fusion_features::Sample a={raw[0]/16384.f,raw[1]/16384.f,raw[2]/16384.f,raw[4]/131.f,raw[5]/131.f,raw[6]/131.f,0};
  ring_[index_]=a;index_=(index_+1)%100;if(count_<100)++count_;
  if(count_<100 || (ready_ && uint32_t(now-evaluated_)<100))return;
  auto& samples=workspace_;for(unsigned i=0;i<100;++i)samples[i]=ring_[(index_+i)%100];
  float f[40],p[2];fusion_features::imu(samples,100,f);motion_model::predict(f,p);
  unsigned mi=motion_model::LABELS[0][0]=='m'?0:1;probability=p[mi];positiveCount_=probability>=.5f?positiveCount_+1:0;ready_=true;evaluated_=now;
 }
 bool confirmed(uint32_t now)const{return ready_ && count_==100 && uint32_t(now-evaluated_)<=200 && probability>=.5f && positiveCount_>=2;}
 bool decisionFresh(uint32_t now)const{return ready_ && count_==100 && uint32_t(now-evaluated_)<=200;}
 void reset(){count_=index_=positiveCount_=0;last_=evaluated_=0;haveLast_=ready_=false;probability=0;}
 unsigned seed(float* ax,float* ay,float* az,float* gx,float* gy,float* gz)const {
  if(count_<100)return 0;
  unsigned start=100;
  for(unsigned i=0;i<100;++i){const auto& a=ring_[(index_+i)%100];if(std::sqrt(a.gx*a.gx+a.gy*a.gy+a.gz*a.gz)>60.f){start=i;break;}}
  if(start==100)return 0;
  unsigned n=0;
  for(unsigned i=start;i<100;++i){const auto& a=ring_[(index_+i)%100];ax[n]=a.ax;ay[n]=a.ay;az[n]=a.az;gx[n]=a.gx;gy[n]=a.gy;gz[n]=a.gz;++n;}
  return n;
 }
 float probability=0;
 private:
 fusion_features::Sample ring_[100],workspace_[100];unsigned count_=0,index_=0,positiveCount_=0;
 uint32_t last_=0,evaluated_=0;bool haveLast_=false,ready_=false;
};
