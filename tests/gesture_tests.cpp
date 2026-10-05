#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <vector>
#include "../GestureVoice/GestureCore.h"
using namespace sixops;

static int checks = 0;
static void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { std::fprintf(stderr, "失败：%s\n", message); std::exit(1); }
}
struct Rig {
  Recognizer r;
  uint32_t now = 0;
  std::vector<char> events;
  Rig() { check(r.configure({3.0f, 2.0f}), "接受分离的握拳与松开阈值"); }
  void run(unsigned duration, float env = 1, char posture = 'n',
           bool valid = true, bool worn = true, bool stable = true) {
    for (unsigned i = 0; i < duration; i += 20) {
      char e = r.update({now, env, posture, valid, worn, stable});
      if (e) events.push_back(e);
      now += 20;
    }
  }
  void arm() { run(400); }
};
int main() {
  // 保留采样解析、包络、肌电阈值与计时回归；姿态学习另在personalized_pose_tests中验证。
  FloatEnvelope env;
  for (int i = 0; i < 32; ++i) env.add(-0.75f);
  check(std::fabs(env.add(-0.75f) - 1.5f) < 0.0001f, "包络保留小数");
  float last = 0;
  for (int i = 0; i < 32; ++i) last = env.add(0);
  check(last == 0, "旧样本全部离开包络窗口");

  uint8_t packet[14] = {0x40,0x00, 0xC0,0x00, 0x20,0x00, 0x00,0x00,
                        0x00,0x83, 0xFF,0x7D, 0x00,0x00};
  Vec3 acceleration, rotation;
  check(!decodeMpu(packet,13,acceleration,rotation), "拒绝不完整的姿态数据");
  check(!decodeMpu(nullptr,14,acceleration,rotation), "拒绝缺失的姿态数据");
  check(decodeMpu(packet,14,acceleration,rotation), "完整姿态数据可以解析");
  check(acceleration.x==1 && acceleration.y==-1 && acceleration.z==0.5f,
        "加速度正负号、比例和顺序正确");
  check(rotation.x==1 && rotation.y==-1 && rotation.z==0,
        "陀螺仪比例正确且跳过温度寄存器");

  float relaxed[100], grip[100];
  for (int i = 0; i < 100; ++i) { relaxed[i] = 1; grip[i] = 7; }
  Thresholds thresholds = {};
  check(gripThresholds(relaxed, grip, 100, thresholds), "分离的肌电参考值可以校准");
  check(thresholds.off > 1 && thresholds.on > thresholds.off && thresholds.on < 7,
        "判定阈值位于两种参考值之间");
  for (int i = 0; i < 100; ++i) grip[i] = 1.2f;
  check(!gripThresholds(relaxed, grip, 100, thresholds), "重叠参考值不能通过校准");
  grip[0] = std::numeric_limits<float>::quiet_NaN();
  check(!gripThresholds(relaxed, grip, 100, thresholds), "拒绝非有限校准数值");
  check(!gripThresholds(nullptr, grip, 100, thresholds), "拒绝缺失的校准参考");
  check(!gripThresholds(relaxed, grip, 0, thresholds), "拒绝空校准数据");

  // 启动时已握住或处于动作姿势，必须先放松回位，不能直接产生操作。
  { Rig x; x.run(2000,7); check(x.events.empty(), "启动时长握不触发"); }
  { Rig x; x.run(2000,1,'w'); check(x.events.empty(), "启动时动作姿势不触发"); }
  const char keys[] = {'w','s','f','b'};
  for (char key:keys) {
    Rig x; x.arm(); x.run(2000,7,key);
    check(x.events.size()==1 && x.events[0]==key, "方向动作伴随肌电变化时仍只触发一次");
    x.arm(); x.run(300,1,key);
    check(x.events.size()==2 && x.events[1]==key, "回到自然位后可再次操作");
  }
  { Rig x; x.arm(); x.run(100,1,'w'); x.arm(); check(x.events.empty(), "忽略短暂方向波动"); }
  { Rig x; x.arm(); x.run(200,1,0,true,true,false); x.run(400,1,'w');
    check(x.events.size()==1 && x.events[0]=='w', "翻腕停稳后不必额外回位即可触发"); }
  { Rig x; x.arm(); x.run(500,1,0); check(x.events.empty(), "未知姿势不是有效操作"); }
  { Rig x; x.arm(); x.run(500,7); check(x.events.empty(), "短握必须等待松开");
    x.run(400); check(x.events.size()==1 && x.events[0]==' ', "短握松开只产生一次空格"); }
  { Rig x; x.arm(); x.run(3000,7); x.run(600);
    check(x.events.size()==1 && x.events[0]=='x', "长握只产生联想指令而不产生空格"); }
  { Rig x; x.arm(); x.run(100,7); x.run(600); check(x.events.empty(), "忽略短暂肌电尖峰"); }
  { Rig x; x.arm(); x.run(1400,7); x.run(400);
    check(x.events.size()==1 && x.events[0]==' ', "松开去抖时间不能把短握变成长握"); }
  { Rig x; x.arm(); x.run(1600,7); x.run(400);
    check(x.events.size()==1 && x.events[0]=='x', "达到长握时长只产生联想指令"); }
  { Rig x; x.arm(); x.run(600,7); x.run(120); x.run(600,7);
    check(x.events.empty(), "短暂包络下降不会提前确认");
    x.run(400); check(x.events.size()==1 && x.events[0]==' ', "短暂下降后真正松开只确认一次"); }
  { Rig x; x.arm(); x.run(600,7); x.run(100,7,'w'); x.run(600);
    check(x.events.empty(), "握拳中转动姿态应取消而不是确认"); }
  { Rig x; x.arm(); x.run(600,7); x.run(20,7,'n',true,false); x.run(2000,7); x.run(400);
    check(x.events.empty(), "未佩戴标记取消待处理握拳"); }
  { Rig x; x.arm(); x.run(600,7); x.run(20,7,'n',false); x.run(2000,7); x.run(400);
    check(x.events.empty(), "传感器异常取消待处理握拳"); }
  { Rig x; x.arm(); x.run(600,7); x.run(20,7,'n',true,true,false); x.run(2000,7); x.run(400);
    check(x.events.empty(), "不稳定运动取消待处理握拳"); }
  { Rig x; x.arm(); x.run(500,7); x.now+=500; x.run(2000,7); x.run(400);
    check(x.events.empty(), "过长更新间隔不能完成握拳指令"); }
  { Rig x; x.arm(); x.run(500,7); x.run(20,std::numeric_limits<float>::quiet_NaN()); x.run(400);
    check(x.events.empty(), "非有限包络不能被解释为松开"); }
  { Rig x; x.now=UINT32_MAX-800; x.arm(); x.run(700,7); x.run(400);
    check(x.events.size()==1 && x.events[0]==' ', "计时器回绕后短握计时仍正确"); }
  { Rig x; x.arm(); x.run(600,7); x.r.reset(); x.run(400);
    check(x.events.empty(), "重新校准取消待确认操作"); }
  { Recognizer x; check(!x.configure({2,3}), "拒绝颠倒的握拳松开阈值");
    char event=0; for(unsigned t=0;t<3000;t+=20) event|=x.update({t,7,'n',true,true,true});
    check(event==0, "无效阈值不能启用操作事件"); }
  { Rig x; x.arm(); x.run(500,7); x.run(20,7,0); x.run(2000,7); x.run(400);
    check(x.events.empty(), "未知姿势取消握拳而不是当作自然位"); }
  { Rig x; x.run(1000,1,0); x.run(500,1,'w');
    check(x.events.empty(), "未知姿势不能代替自然位重新准备"); }
  { Rig x; x.arm(); x.run(100,1,'w'); x.run(100,1,0); x.run(100,1,'w');
    check(x.events.empty(), "未知姿势会打断方向确认时长"); }
  { Rig x; x.arm(); x.run(100,1,'w'); x.run(100,1,'s');
    check(x.events.empty(), "不同方向不合并确认时长"); }
  { Rig x; x.arm(); x.run(400,1,'?'); check(x.events.empty(), "非法姿势标签锁定且不输出"); }
  std::printf("通过：%d项采样与动作检查\n", checks);
}
