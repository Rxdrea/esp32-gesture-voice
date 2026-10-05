#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <vector>
#include "../GestureVoice/GestureCore.h"
using namespace sixops;
static unsigned checks=0;
static void check(bool ok,const char* message) {
  ++checks;
  if (!ok) { std::fprintf(stderr,"失败：%s\n",message); std::exit(1); }
}
static Vec3 tilt(float degrees,float azimuth=0) {
  const float a=degrees/57.2957795f,b=azimuth/57.2957795f;
  return {sinf(a)*cosf(b),sinf(a)*sinf(b),cosf(a)};
}
static Vec3 rotate(Vec3 a) { return {a.z,-a.x,-a.y}; }
static bool teach(PoseMap& p,char key,Vec3 v) {
  Vec3 samples[100]; for (auto& x:samples) x=v;
  return p.setReference(key,samples,100);
}
static void fixture(PoseMap& p,bool rotated=false) {
  const Vec3 references[]={{0,0,1},tilt(107.8f),tilt(18,180),tilt(35,70),tilt(26,260)};
  const char keys[]={'n','w','s','f','b'};
  for (unsigned i=0;i<5;++i) check(teach(p,keys[i],rotated?rotate(references[i]):references[i]),"分别学习五个实际姿势");
}
int main() {
  PoseMap p;
  check(p.classify({0,0,1})==0,"未学习不能识别");
  check(teach(p,'n',{0,0,1}),"自然位可记录");
  check(teach(p,'w',tilt(107.8f)),"107.8度不被固定65度上限拒绝");
  check(!p.ready() && p.classify(tilt(107.8f))==0,"只学上翘不能提前启用识别");
  check(teach(p,'s',tilt(18,180)),"低于22度的独立下压可以学习");
  check(teach(p,'f',tilt(35,70)),"侧翻允许与原上下轴有交叉");
  check(teach(p,'b',tilt(26,260)) && p.ready(),"另一侧独立学习后模型完整");
  check(p.classify({0,0,1})=='n',"自然位匹配");
  check(p.classify(tilt(107.8f))=='w',"大于90度上翘运行时也能识别");
  check(p.classify(tilt(18,180))=='s',"下压不是上翘的镜像推断");
  check(p.classify(tilt(35,70))=='f',"斜向但明确的侧翻可识别");
  check(p.classify(tilt(26,260))=='b',"非对称反向侧翻可识别");
  check(p.classify(tilt(160,130))==0,"远离所有模板的姿势不能强行归类");
  check(p.classify(tilt(9,180))==0,"两模板中间的模糊姿势不触发");
  check(p.classify({0,0,0})==0,"拒绝零向量");
  check(p.classify({NAN,0,1})==0 && p.classify({INFINITY,0,1})==0,"拒绝非有限测量");
  check(p.classify({0,0,1.1f})=='n',"以方向而非模长匹配");
  PoseMap installed; fixture(installed,true);
  check(installed.classify(rotate(tilt(107.8f)))=='w',"旋转安装不改变学习动作标签");
  check(installed.classify(rotate(tilt(18,180)))=='s',"旋转安装识别非对称下压");
  PoseMap tiny;
  check(teach(tiny,'n',{0,0,1}) && teach(tiny,'w',tilt(0.4f)),"可重复的小幅度不受固定最低动作角限制");
  check(teach(tiny,'s',tilt(0.6f,180)) && teach(tiny,'f',tilt(0.8f,90)) && teach(tiny,'b',tilt(1.0f,270)),"小幅模板分别记录");
  check(tiny.classify(tilt(0.4f))=='w',"稳定小幅动作运行时触发");
  check(tiny.classify(tilt(0.2f))==0,"小幅动作仍有未知区域");
  PoseMap duplicate; check(teach(duplicate,'n',{0,0,1}),"准备重复参考");
  check(!teach(duplicate,'w',{0,0,1}) && duplicate.conflictKey()=='n',"上翘与自然位无法区分时明确拒绝");
  check(teach(duplicate,'w',tilt(30)),"失败后当前步骤可重试");
  check(!teach(duplicate,'s',tilt(30)) && duplicate.conflictKey()=='w',"不接受同姿势两个动作");
  check(!duplicate.ready(),"重复参考不能启用模型");
  PoseMap noisy; Vec3 jitter[100];
  for (unsigned i=0;i<100;++i) jitter[i]=tilt(i%2?0.5f:-0.5f);
  check(noisy.setReference('n',jitter,100),"记录自然位实际波动");
  for (unsigned i=0;i<100;++i) jitter[i]=tilt(i%2?2.0f:1.0f);
  check(!noisy.setReference('w',jitter,100),"波动覆盖动作差异时拒绝混淆模板");
  for (unsigned i=0;i<100;++i) jitter[i]=tilt(i%2?19.0f:21.0f);
  check(noisy.setReference('w',jitter,100),"分离足够时接受实际波动");
  check(teach(noisy,'s',tilt(30,180)) && teach(noisy,'f',tilt(35,90)) && teach(noisy,'b',tilt(25,270)),"完成波动参考");
  check(noisy.classify(tilt(19))=='w' && noisy.classify(tilt(21))=='w',"接受采集波动内的动作");
  check(noisy.spreadDegrees('w')>0.9f && noisy.spreadDegrees('w')<1.1f,"参考波动来自数据");
  check(noisy.radiusDegrees('w')>noisy.spreadDegrees('w'),"识别范围涵盖采集波动");
  PoseMap steady;
  check(teach(steady,'n',{0,0,1}) && teach(steady,'w',tilt(20)) &&
        teach(steady,'s',tilt(30,180)) && teach(steady,'f',tilt(35,90)) &&
        teach(steady,'b',tilt(25,270)),"准备相同参考中心且波动更小的对照");
  check(noisy.radiusDegrees('w')>steady.radiusDegrees('w')+0.5f,
        "同样的动作间距下识别范围随实际采集波动调整");
  Vec3 invalid[100]; for (auto& x:invalid) x=tilt(35,90); invalid[50]={NAN,0,0};
  check(!noisy.setReference('f',invalid,100) && !noisy.ready(),"重学失败后不能沿用旧完整模型");
  check(noisy.hasReference('s') && !noisy.hasReference('f') && !noisy.hasReference('b'),"保留前面步骤并清除本步及后续模板");
  check(teach(p,'n',{1,0,0}) && !p.ready() && !p.hasReference('w'),"重设自然位清空所有动作参考");
  check(!p.setReference('w',nullptr,100),"拒绝缺失数据");
  Vec3 few[2]={{0,0,1},{0,0,1}};
  check(!p.setReference('w',few,2),"不足够样本不能伪通过");
  PoseMap sequence;
  check(!teach(sequence,'w',tilt(30)) && !sequence.ready(),"不能跳过自然位");
  check(!teach(sequence,'?',{0,0,1}),"未知标签不改变模型");
  PoseMap model; fixture(model);
  Recognizer recognizer; check(recognizer.configure({3,2}),"准备实际模板到事件全链路");
  uint32_t now=0; std::vector<char> events;
  const auto run=[&](Vec3 v,unsigned duration,float env) {
    for(unsigned i=0;i<duration;i+=20) {
      char event=recognizer.update({now,env,model.classify(v),true,true,true});
      if(event) events.push_back(event);
      now+=20;
    }
  };
  run({0,0,1},400,1); run(tilt(107.8f),2000,1);
  check(events.size()==1 && events[0]=='w',"大幅校准姿势真实产生一次w而非只校准通过");
  run({0,0,1},400,1); run(tilt(18,180),400,1);
  run({0,0,1},400,1); run(tilt(35,70),400,1);
  run({0,0,1},400,1); run(tilt(26,260),400,1);
  check(events==std::vector<char>({'w','s','f','b'}),"四个独立实测姿势各产生预期事件");
  run({0,0,1},400,1); run({0,0,1},600,7); run({0,0,1},400,1);
  check(events.back()==' ' && events.size()==5,"个性化自然位短握松开确认");
  run({0,0,1},400,1); run({0,0,1},2000,7); run({0,0,1},400,1);
  check(events.back()=='x' && events.size()==6,"个性化自然位长握仅联想一次");
  std::printf("通过：%u项个性化姿势检查\n",checks);
}
