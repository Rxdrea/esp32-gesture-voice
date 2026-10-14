#pragma once
#include <stdint.h>
#include "FusionFeatures.h"
#include "GripModel.h"
#include "MotionGuardModel.h"
namespace fusion {
constexpr float GRIP_PROBABILITY=.70f, MOTION_PROBABILITY=.50f;
class Core {
 public:
 bool update(uint32_t now,const fusion_features::Sample& s,bool valid,bool worn) {
  uint32_t dt=now-lastMs_;bool gap=haveLast_ && dt>40;lastMs_=now;haveLast_=true;
  float norm=std::sqrt(s.ax*s.ax+s.ay*s.ay+s.az*s.az);
  rate=std::sqrt(s.gx*s.gx+s.gy*s.gy+s.gz*s.gz);
  for(float v:{s.ax,s.ay,s.az,s.gx,s.gy,s.gz,s.emg})if(!std::isfinite(v))valid=false;
  if(!valid || !worn || s.emg<0 || norm<.25f || norm>3.5f || gap) {
   fault=!worn?2:gap?4:6;inhibit(now);hint=!worn?"未检测到稳定佩戴":gap?"数据间隔中断":"传感数据无效";return false;
  }
  float alignment=(s.ax*neutral_[0]+s.ay*neutral_[1]+s.az*neutral_[2])/norm;
  angle=neutralReady?std::acos(std::max(-1.f,std::min(1.f,alignment)))*57.29578f:0;
  if(uint32_t(now-blockAt_)<500){hint="动作后等待稳定";return false;}
  ring_[index_]=s;index_=(index_+1)%100;if(count<100)++count;
  const float onsetThreshold=baseline_+std::max(1.5f,baseline_*.25f);
  bool high=s.emg>onsetThreshold;
  if(armed) {
   if(high){if(!onset_){onset_=true;onsetAt_=now;}low_=false;}
   else if(onset_){if(!low_){low_=true;lowAt_=now;}if(uint32_t(now-lowAt_)>=200)onset_=false;}
  }
  if(count<100){hint=neutralReady?"收集新的肌电窗口":"自然位放松一秒";return false;}
  if(haveEvaluation_ && uint32_t(now-evaluatedAt_)<100)return false;
  haveEvaluation_=true;evaluatedAt_=now;
  auto& samples=workspace_;for(unsigned i=0;i<100;++i)samples[i]=ring_[(index_+i)%100];
  float ef[16],mf[40],ep[2],mp[2];fusion_features::emg(samples,ef);fusion_features::imu(samples,100,mf);
  grip_model::predict(ef,ep);motion_model::predict(mf,mp);
  const unsigned gi=grip_model::LABELS[0][0]=='g'?0:1;
  const unsigned mi=motion_model::LABELS[0][0]=='m'?0:1;
  gripProbability=ep[gi];motionProbability=mp[mi];above10=unsigned(ef[13]*100+.5f);
  if(motionProbability>=MOTION_PROBABILITY) {
   if(armed || onset_){++resets;}
   armed=false;onset_=false;restCount_=0;fault=10;hint="手腕动作优先识别";return false;
  }
  bool relaxed=gripProbability<=.30f;
  float center[3]={};
  for(const auto& a:samples){float n=std::sqrt(a.ax*a.ax+a.ay*a.ay+a.az*a.az);center[0]+=a.ax/n;center[1]+=a.ay/n;center[2]+=a.az/n;}
  float length=std::sqrt(center[0]*center[0]+center[1]*center[1]+center[2]*center[2]);
  if(length<1){hint="姿态参考暂不可用";return false;}
  for(float& v:center)v/=length;
  bool steady=true;
  for(const auto& a:samples){float n=std::sqrt(a.ax*a.ax+a.ay*a.ay+a.az*a.az);if((a.ax*center[0]+a.ay*center[1]+a.az*center[2])/n<std::cos(20.f/57.29578f))steady=false;}
  if(!neutralReady) {
   if(!relaxed || !steady){hint="请自然位放松记录";return false;}
   for(unsigned k=0;k<3;++k)anchor_[k]=neutral_[k]=center[k];
   neutralReady=true;baseline_=ef[6];armed=true;onset_=false;hint="已放松可以握拳";return false;
  }
  if(angle>60){if(armed)++resets;armed=false;onset_=false;restCount_=0;fault=8;hint="请回大致自然位置";return false;}
  if(relaxed && steady && !onset_) {
   baseline_=.9f*baseline_+.1f*ef[6];
   float target[3];for(unsigned k=0;k<3;++k)target[k]=.95f*neutral_[k]+.05f*center[k];
   float n=std::sqrt(target[0]*target[0]+target[1]*target[1]+target[2]*target[2]);for(float& v:target)v/=n;
   if(target[0]*anchor_[0]+target[1]*anchor_[1]+target[2]*anchor_[2]>=std::cos(20.f/57.29578f))for(unsigned k=0;k<3;++k)neutral_[k]=target[k];
  }
  if(!armed) {
   restCount_=relaxed?restCount_+1:0;
   if(restCount_>=2){armed=true;onset_=false;restCount_=0;hint="已放松可以握拳";}else hint="先放松后再握拳";
   return false;
  }
  hint=gripProbability>=GRIP_PROBABILITY?"握拳请保持一秒":relaxed?"已放松可以握拳":"正在分辨肌电变化";
  if(onset_ && high && uint32_t(now-onsetAt_)>=1000 && gripProbability>=GRIP_PROBABILITY) {
   armed=false;onset_=false;restCount_=0;++events;hint="握拳联想已触发";return true;
  }
  return false;
 }
 void inhibit(uint32_t now) {
  if(armed || count)++resets;
  armed=false;onset_=false;low_=false;count=index_=restCount_=0;haveEvaluation_=false;blockAt_=now;
  gripProbability=motionProbability=0;
 }
 // 原地重新记录自然位，不构造含数百帧数组的大临时对象。
 void reset() {
  neutralReady=armed=false;count=resets=events=fault=above10=0;
  gripProbability=motionProbability=rate=angle=0;
  index_=restCount_=0;baseline_=2;
  for(unsigned k=0;k<3;++k)neutral_[k]=anchor_[k]=k==2?1.f:0.f;
  lastMs_=blockAt_=evaluatedAt_=onsetAt_=lowAt_=0;
  haveLast_=haveEvaluation_=onset_=low_=false;hint="等待一秒传感数据";
 }
 bool motionDecisionFresh(uint32_t now)const{return count==100 && haveEvaluation_ && uint32_t(now-evaluatedAt_)<=200;}
 bool neutralReady=false,armed=false;unsigned count=0,resets=0,events=0,fault=0,above10=0;
 float gripProbability=0,motionProbability=0,rate=0,angle=0;
 const char* hint="等待一秒传感数据";
 private:
 // 识别仅由主循环访问；工作帧保存在对象中，避免挤占任务调用栈。
 fusion_features::Sample ring_[100],workspace_[100];unsigned index_=0,restCount_=0;
 float neutral_[3]={0,0,1},anchor_[3]={0,0,1},baseline_=2;
 uint32_t lastMs_=0,blockAt_=0,evaluatedAt_=0,onsetAt_=0,lowAt_=0;
 bool haveLast_=false,haveEvaluation_=false,onset_=false,low_=false;
};
}
