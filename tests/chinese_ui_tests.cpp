#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../GestureVoice/GestureCore.h"

static void require(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "失败：%s\n", message); std::exit(1); }
}
int main() {
  sixops::Recognizer r;
  require(std::strcmp(r.stateName(), "请先校准")==0, "未校准时应显示中文提示");
  require(r.configure({3,2}), "准备识别测试");
  require(std::strcmp(r.stateName(), "回位并放松")==0, "等待回位时应显示中文提示");
  for (unsigned t=0;t<400;t+=20) r.update({t,1,'n',true,true,true});
  require(std::strcmp(r.stateName(), "可以操作")==0, "准备完成时应显示中文提示");
  for (unsigned t=400;t<700;t+=20) r.update({t,7,'n',true,true,true});
  require(std::strcmp(r.stateName(), "握住或松开")==0, "握拳过程中应显示中文提示");
  unsigned spaces=0, other=0;
  for (unsigned t=700;t<1100;t+=20) {
    char key=r.update({t,1,'n',true,true,true});
    if (key==' ') ++spaces;
    else if (key) ++other;
  }
  require(spaces==1 && other==0, "中文提示不能改变短握的空格指令");
  std::puts("通过：中文状态提示与原操作指令保持一致");
}
