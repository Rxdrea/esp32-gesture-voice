#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../GestureVoice/GestureCore.h"
using namespace sixops;
static unsigned checks=0;
static void check(bool ok,const char* message) {
  ++checks;
  if (!ok) { std::fprintf(stderr,"失败：%s\n",message); std::exit(1); }
}
struct Rig {
  Recognizer r;
  uint32_t now=0;
  std::vector<char> events;
  Rig() { check(r.configure({3,2}),"准备识别器"); }
  void run(unsigned ms,char pose='n',float env=1,bool valid=true,bool worn=true,bool stable=true) {
    for (unsigned i=0;i<ms;i+=20) {
      const char e=r.update({now,env,pose,valid,worn,stable});
      if(e) events.push_back(e);
      now+=20;
    }
  }
  void arm() { run(400); }
};
static void motion_then_direction() {
  for(char key:std::vector<char>{'w','s','f','b'}) {
    Rig x; x.arm(); x.run(160,'n',7); x.run(100,0,7,true,true,false);
    x.run(1000,key,7);
    check(x.events==std::vector<char>{key},"肌电先升高再转腕，停稳后只触发一次方向");
    x.run(2000,key,7); check(x.events.size()==1,"保持动作不连续翻页");
  }
}
static void direct_pose_change() {
  Rig x; x.arm(); x.run(600,'n',7); x.run(400,'s',7);
  check(x.events==std::vector<char>{'s'},"取消握拳后稳定方向可触发，不要求额外回位");
}
static void cancelled_grip_stays_cancelled() {
  Rig x; x.arm(); x.run(600,'n',7); x.run(20,0,7,true,true,false);
  x.run(2000,'n',7); x.run(400);
  check(x.events.empty(),"取消的握拳不会恢复为长握或松开确认");
  x.run(600,'n',7); x.run(400);
  check(x.events==std::vector<char>{' '},"回位放松后新的短握正常确认");
}
static void partial_direction_cancelled() {
  Rig x; x.arm(); x.run(600,'n',7); x.run(100,'w',7); x.run(600);
  check(x.events.empty(),"短暂方向未停稳时既不翻页也不确认旧握拳");
}
static void real_fault_still_locks() {
  for(int fault=0;fault<2;++fault) {
    Rig x; x.arm(); x.run(160,'n',7); x.run(100,0,7,true,true,false);
    x.run(20,'w',7,fault!=0,fault!=1); x.run(1000,'w',7);
    check(x.events.empty(),"真实数据错误或未佩戴仍锁定方向");
    x.arm(); x.run(400,'w'); check(x.events==std::vector<char>{'w'},"故障后回位放松才恢复");
  }
}
static void startup_and_neutral_stay_gated() {
  Rig x; x.run(100,0,7,true,true,false); x.run(1000,'w',7);
  check(x.events.empty(),"启动时运动不授予方向操作权限");
  x.arm(); x.run(400,'w'); x.run(600,'n',2.2f); x.run(400,'s');
  check(x.events==std::vector<char>{'w'},"不擅自放宽既有放松阈值");
}
// 取消握拳后，短暂回自然位也必须打断之前的方向停稳计时。
static void neutral_restarts_cancelled_grip_direction() {
  for(float env:std::vector<float>{1,7}) {
    Rig x; x.arm(); x.run(160,'n',7); x.run(20,0,7,true,true,false);
    x.run(100,'w',7); x.run(100,'n',env); x.run(20,'w',7);
    check(x.events.empty(),"短暂回自然位后再次上翘不能沿用旧方向计时");
    x.run(160,'w',7);
    check(x.events.empty(),"再次上翘未连续停稳180毫秒不能触发");
    x.run(20,'w',7);
    check(x.events==std::vector<char>{'w'},"重新连续停稳180毫秒只触发一次方向");
  }
}
// 回位中途离开后必须重新连续计时，不能拼接两段放松时间。
static void interrupted_neutral_does_not_rearm() {
  Rig x; x.arm(); x.run(160,'n',7); x.run(20,0,7,true,true,false);
  x.run(200); x.run(100,0); x.run(140); x.run(2000,'n',7);
  check(x.events.empty(),"断续回位不能解锁旧握拳");
  x.arm(); x.run(600,'n',7); x.run(400);
  check(x.events==std::vector<char>{' '},"完整回位后新握拳才能确认");
}
// 起始握拳尚未确认时发生移动，也必须丢弃该次握拳。
static void stable_pose_cancels_pending_grip() {
  for(char pose:std::vector<char>{'w',0}) {
    Rig x; x.arm(); x.run(80,'n',7); x.run(100,pose,7);
    x.run(2000,'n',7); x.run(400);
    check(x.events.empty(),"缓慢离开自然位也应取消起始握拳，不能回位后自动长握");
    x.run(600,'n',7); x.run(400);
    check(x.events==std::vector<char>{' '},"缓慢转腕后重新放松才可确认新握拳");
    Rig direction; direction.arm(); direction.run(80,'n',7); direction.run(100,pose,7);
    direction.run(400,'w',7);
    check(direction.events==std::vector<char>{'w'},"取消起始握拳后仍保留方向停稳操作资格");
  }
}
static void motion_cancels_pending_grip() {
  Rig x; x.arm(); x.run(80,'n',7); x.run(20,0,7,true,true,false);
  x.run(2000,'n',7); x.run(400);
  check(x.events.empty(),"移动不能让未确认的握拳重新累计为长握");
  x.run(600,'n',7); x.run(400);
  check(x.events==std::vector<char>{' '},"重新放松后可以进行新短握");
}
static void contact_pause_cancels_grip() {
  for(unsigned held:std::vector<unsigned>{80,600}) {
    Rig x; x.arm(); x.run(held,'n',7);
    x.r.pauseContact(x.now); x.now+=20;
    x.run(2000,'n',7); x.run(400);
    check(x.events.empty(),"接触暂停取消起始或进行中的握拳");
    x.run(600,'n',7); x.run(400);
    check(x.events==std::vector<char>{' '},"接触恢复并放松后才可重新确认");
  }
}
static void contact_pause_restarts_direction() {
  Rig x; x.arm(); x.run(120,'w');
  for(unsigned i=0;i<20;++i) { x.r.pauseContact(x.now); x.now+=20; }
  x.run(160,'w');
  check(x.events.empty(),"接触暂停时间不能计入方向停稳时间");
  x.run(60,'w');
  check(x.events==std::vector<char>{'w'},"恢复后重新停稳才能触发方向");
}
static void contact_pause_restarts_neutral() {
  Rig x; x.run(200); x.r.pauseContact(x.now); x.now+=20; x.run(140);
  x.run(400,'w'); check(x.events.empty(),"接触暂停不能拼接回位计时");
  x.arm(); x.run(400,'w'); check(x.events==std::vector<char>{'w'},"连续回位后恢复方向");
}
static void contact_pause_does_not_bypass_faults() {
  Rig startup; startup.r.pauseContact(startup.now); startup.now+=20;
  startup.run(400,'w'); check(startup.events.empty(),"启动暂停不授予方向资格");
  Rig gap; gap.arm(); gap.now+=121; gap.r.pauseContact(gap.now); gap.now+=20;
  gap.run(400,'w'); check(gap.events.empty(),"长采样间隔仍锁定");
  for(int fault=0;fault<2;++fault) {
    Rig x; x.arm(); x.run(20,'n',1,fault!=0,fault!=1);
    x.r.pauseContact(x.now); x.now+=20; x.run(400,'w');
    check(x.events.empty(),"真实故障后暂停不能恢复方向资格");
  }
}
static void wear_startup_and_brief_dip() {
  WearFilter w;
  check(!w.update(0,false) && !w.uncertain(),"启动低电平不算已佩戴");
  check(!w.update(100,true) && !w.update(139,true),"佩戴恢复必须持续稳定");
  check(w.update(140,true),"稳定高电平后恢复佩戴");
  check(w.update(200,false) && w.uncertain(),"短暂低电平立即暂停而不确认脱落");
  check(w.update(206,true) && w.uncertain(),"短暂恢复不能立即接收输入");
  check(w.update(245,true) && w.uncertain(),"恢复确认前仍暂停");
  check(w.update(246,true) && !w.uncertain(),"恢复稳定后结束暂停");
}
static void wear_continuous_low_and_chatter() {
  WearFilter w; w.update(0,true); w.update(40,true);
  check(w.update(100,false) && w.update(199,false),"确认脱落前保留暂停态");
  check(!w.update(200,false) && !w.update(300,false),"持续低电平确认脱落");
  check(!w.update(310,true) && !w.update(340,false),"未稳定恢复又变低不能解锁");
  check(!w.update(350,true) && w.update(390,true),"真正脱落后仍需稳定高电平");
  for(unsigned t=400;t<600;t+=20)
    check(w.update(t,((t-400)/20)%2!=0) && w.uncertain(),"交替抖动期间始终暂停");
  check(!w.update(600,false),"反复抖动达到期限确认故障");
}
static void time_wrap_keeps_contact_and_actions_correct() {
  WearFilter w;
  const uint32_t start=UINT32_MAX-20u;
  check(!w.update(start,true) && !w.update(start+39u,true) && w.update(start+40u,true),
      "计时回绕不提前结束恢复等待");
  WearFilter dip; dip.update(start-100u,true); dip.update(start-60u,true);
  check(dip.update(start,false) && dip.update(start+99u,false) && !dip.update(start+100u,false),
      "计时回绕仍能确认持续脱落");
  Rig x; x.now=UINT32_MAX-500u; x.arm(); x.run(100,'w');
  x.r.pauseContact(x.now); x.now+=20; x.run(220,'w');
  check(x.events==std::vector<char>{'w'},"计时回绕后方向重新停稳正常");
}
struct Case { const char* name; void (*run)(); };
static const Case cases[]={
  {"motion_then_direction",motion_then_direction},
  {"direct_pose_change",direct_pose_change},
  {"cancelled_grip_stays_cancelled",cancelled_grip_stays_cancelled},
  {"partial_direction_cancelled",partial_direction_cancelled},
  {"real_fault_still_locks",real_fault_still_locks},
  {"startup_and_neutral_stay_gated",startup_and_neutral_stay_gated},
  {"neutral_restarts_cancelled_grip_direction",neutral_restarts_cancelled_grip_direction},
  {"interrupted_neutral_does_not_rearm",interrupted_neutral_does_not_rearm},
  {"motion_cancels_pending_grip",motion_cancels_pending_grip},
  {"stable_pose_cancels_pending_grip",stable_pose_cancels_pending_grip},
  {"contact_pause_cancels_grip",contact_pause_cancels_grip},
  {"contact_pause_restarts_direction",contact_pause_restarts_direction},
  {"contact_pause_restarts_neutral",contact_pause_restarts_neutral},
  {"contact_pause_does_not_bypass_faults",contact_pause_does_not_bypass_faults},
  {"wear_startup_and_brief_dip",wear_startup_and_brief_dip},
  {"wear_continuous_low_and_chatter",wear_continuous_low_and_chatter},
  {"time_wrap_keeps_contact_and_actions_correct",time_wrap_keeps_contact_and_actions_correct}
};
int main(int argc,char** argv) {
  unsigned ran=0;
  for(const Case& c:cases) {
    if(argc>1 && std::strcmp(argv[1],c.name)!=0) continue;
    c.run(); ++ran; std::printf("PASS %s\n",c.name);
  }
  check(ran>0,"至少运行一项用例");
  std::printf("通过：%u项容错场景，%u个检查\n",ran,checks);
}
