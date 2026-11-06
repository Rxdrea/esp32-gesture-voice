#pragma once
#include "DemoModel.h"
// 只读MPU；演示模式独占动作窗口，画完停住后发出一次事件。
class DemoRecognizer {
public:
 enum class State { Waiting,Ready,Capture,Recovery };
 State state=State::Waiting;
 const char* hint="先放松片刻";
 float nearest=0,alternative=0,durationMs=0;
 uint32_t inferenceUs=0,evaluationSequence=0;
 int lastLabel=-1;
 void reset(){
  state=State::Waiting;hint="先放松片刻";count=prefixCount=prefixAt=onset=0;
  quietPending=false;haveTime=false;nearest=alternative=durationMs=0;inferenceUs=0;
 }
 void invalid(const char* why){reset();hint=why;}
 bool ready()const{return state==State::Ready;}
 int update(uint32_t nowMs,uint32_t nowUs,const int16_t* raw){
  demo_features::Sample sample;sample.us=nowUs;
  constexpr unsigned axis[]={0,1,2,4,5,6};
  for(unsigned i=0;i<6;++i){
   int value=raw[axis[i]];
   if(value>=32700 || value<=-32700){invalid("动作超出量程 重做");return -1;}
   sample.v[i]=value/(i<3?8192.f:65.5f);
  }
  if(haveTime && uint32_t(nowUs-lastUs)>40000){invalid("采样中断 请重做");return -1;}
  lastUs=nowUs;haveTime=true;
  const float speed2=demo_features::rate2(sample);
  const bool quiet=speed2<=25.f*25.f;
  if(state==State::Waiting || state==State::Recovery){
   if(state==State::Recovery && uint32_t(nowMs-recoveryAt)<900){quietPending=false;return -1;}
   if(!quiet){quietPending=false;return -1;}
   if(!quietPending){quietPending=true;quietAt=nowMs;}
   if(uint32_t(nowMs-quietAt)>=500){
    state=State::Ready;hint="准备好 画三角形或Z";quietPending=false;prefixCount=prefixAt=onset=0;
   }
   return -1;
  }
  if(state==State::Ready){
   prefix[prefixAt]=sample;prefixAt=(prefixAt+1)%PREFIX;prefixCount=std::min(PREFIX,prefixCount+1);
   onset=speed2>35.f*35.f?onset+1:0;
   if(onset<3)return -1;
   count=prefixCount;
   for(unsigned i=0;i<count;++i)samples[i]=prefix[(prefixAt+PREFIX-count+i)%PREFIX];
   state=State::Capture;hint="正在识别 画完停住";quietPending=false;captureAt=nowMs;
   return -1;
  }
  if(count>=MAX_SAMPLES || uint32_t(nowMs-captureAt)>6000){recover(nowMs,"动作过长 请重新画");return -1;}
  samples[count++]=sample;
  if(!quiet){quietPending=false;return -1;}
  if(!quietPending){quietPending=true;quietAt=nowMs;}
  // 当前训练记录的角点停顿最长约430毫秒，500毫秒结束确认保留完整图形。
  if(uint32_t(nowMs-quietAt)<500)return -1;
  if(!demo_features::extract(samples,count,pattern,durationMs)){recover(nowMs,"未匹配 请重新画");return -1;}
  const uint32_t started=micros();
  const demo_model::Result result=demo_model::predict(pattern,durationMs);
  inferenceUs=micros()-started;++evaluationSequence;lastLabel=result.label;
  nearest=result.nearest;alternative=result.alternative;
  // 返回事件前先锁定；拒绝、播报结束或重新连接后均需重新放松。
  recover(nowMs,result.label==0?"三角形 我想吃饭":result.label==1?"Z 你们好啊":"未匹配 请重新画");
  return result.label;
 }
private:
 static constexpr unsigned MAX_SAMPLES=600,PREFIX=20;
 demo_features::Sample samples[MAX_SAMPLES],prefix[PREFIX];
 demo_features::Pattern pattern;
 unsigned count=0,prefixCount=0,prefixAt=0,onset=0;
 bool quietPending=false,haveTime=false;
 uint32_t quietAt=0,lastUs=0,captureAt=0,recoveryAt=0;
 void recover(uint32_t now,const char* why){
  state=State::Recovery;hint=why;recoveryAt=now;count=prefixCount=prefixAt=onset=0;quietPending=false;
 }
};
