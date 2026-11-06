#pragma once
// 本版活动识别入口：已验证的纯MPU手腕模型与肌电握拳模型。
// 当前仅保留模型识别入口，废弃六步规则源码已移除。
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <CheezsEMG.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_system.h>
#endif
#include "WristModel.h"
#include "WristMotionGate.h"
#include "FusionCore.h"
#include "HoldScroll.h"
#include "DemoRecognizer.h"
#include "DemoDisplay.h"

namespace gesture {
float resultConf=0;
bool demoMode=true;
int demoAuthorized=-1;
DemoRecognizer demoRecognizer;
#include "EmgRecognition.h"
constexpr uint8_t RESET_BUTTON_PIN=33;
constexpr uint32_t RESET_HOLD_MS=1000;
HardwareSerial bluetoothSerial(2),extraSerial(1);
constexpr uint32_t EXTRA_SERIAL_BAUDRATE=9600;


// HC-04改由BrainRemote后台任务独占读取，主循环只取快照。

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0,U8X8_PIN_NONE,22,21);
bool samplingReady=false,oledReady=false,mpuReady=false;
bool oledEnabled=true;
float panelImu[6]={};
// 仅主循环写入真实成功采样；HTTP只读取此固定快照，不访问Wire。
struct PanelImuView { float axes[6]={}; uint32_t timeMs=0,sequence=0; bool valid=false; };
PanelImuView panelImuView;
portMUX_TYPE panelImuMux=portMUX_INITIALIZER_UNLOCKED;
void invalidatePanelImu(){portENTER_CRITICAL(&panelImuMux);panelImuView.valid=false;portEXIT_CRITICAL(&panelImuMux);}
void publishPanelImu(uint32_t now){
 portENTER_CRITICAL(&panelImuMux);
 memcpy(panelImuView.axes,panelImu,sizeof(panelImu));panelImuView.timeMs=now;
 ++panelImuView.sequence;panelImuView.valid=true;
 portEXIT_CRITICAL(&panelImuMux);
}
PanelImuView panelImuSnapshot(){
 portENTER_CRITICAL(&panelImuMux);PanelImuView value=panelImuView;portEXIT_CRITICAL(&panelImuMux);return value;
}
float panelModelMs=0,panelDistance=0,panelAlternative=0,panelDuration=0;
bool panelModelValid=false;uint32_t panelModelAt=0;int panelModelLabel=-1;
uint8_t mpuAddress=0x68,mpuFailures=0;
uint32_t lastDrawMs=0,lastControlMs=0,lastMpuMs=0,lastRetryMs=0;
bool haveMpu=false,diagnostics=false;
uint32_t longestControlGap=0,captureGapDrops=0;
enum class Phase { Idle,Capture,Cooldown,Scroll };
Phase phase=Phase::Idle;
WristMotionGate wristGate;
HoldScroll holdScroll;
uint32_t repeatIntervalMs=300;
constexpr uint32_t ACTION_COOLDOWN_MS=350;
constexpr unsigned WINDOW=200;
float ax[WINDOW],ay[WINDOW],az[WINDOW],gx[WINDOW],gy[WINDOW],gz[WINDOW];
unsigned captureCount=0;
uint32_t phaseAt=0;
bool requireQuiet=false,quietPending=false;
uint32_t quietAt=0;
char authorizedKey=0;
const char* lastAction="等待动作";

// OLED分片刷新：每次只画一行或传输8像素高的一行。
char screen[5][96]={},frame[5][96]={};
uint32_t screenVersion=0,frameVersion=0,lastSliceMs=0;
uint8_t displayStage=13;int largeLine=-1,frameLargeLine=-1;
int keypadSelection=-1,frameKeypadSelection=-1;
uint8_t screenLayout=0,frameLayout=0;
void queueScreen(const char* a,const char* b,const char* c,const char* d,const char* e=nullptr,int large=-1,int keypad=-1,uint8_t layout=0) {
 const char* lines[]={a,b,c,d,e};char normalized[5][96]={};bool changed=largeLine!=large || keypadSelection!=keypad || screenLayout!=layout;
 for(unsigned i=0;i<5;++i){
  const char* text=lines[i]?lines[i]:"";size_t n=std::min(std::strlen(text),sizeof(normalized[i])-1);
  // 从截断处退回完整UTF-8字符边界，比较的也是最终实际显示内容。
  while(n && (uint8_t(text[n])&0xC0)==0x80)--n;
  std::memcpy(normalized[i],text,n);
  if(std::strcmp(screen[i],normalized[i])!=0)changed=true;
 }
 if(!changed)return;
 std::memcpy(screen,normalized,sizeof(screen));
 largeLine=large;keypadSelection=keypad;screenLayout=layout;++screenVersion;
}
void queueDemoScreen(const char* message,const char* footer,const char* badge){
 queueScreen("手势语音",message,footer,badge,nullptr,-1,-1,1);
}
void invalidateDisplay(){displayStage=13;frameVersion=screenVersion-1;}
void serviceDisplay() {
 if(!oledReady || !oledEnabled || uint32_t(millis()-lastSliceMs)<10)return;
 lastSliceMs=millis();
 if(displayStage>=13){if(frameVersion==screenVersion)return;std::memcpy(frame,screen,sizeof(frame));frameLargeLine=largeLine;frameKeypadSelection=keypadSelection;frameLayout=screenLayout;frameVersion=screenVersion;display.clearBuffer();display.setFontPosBaseline();displayStage=0;}
 if(displayStage<5){
  const unsigned row=displayStage;
  if(frameLayout==1){
    demo_ui::drawSlice(display,row,frame[0],frame[1],frame[2],frame[3]);
  } else if(frameKeypadSelection>=0 && row>=1 && row<=3){
    // 九宫格仍按行绘制、按8像素分片传输，不整屏阻塞采样。
    display.setFont(u8g2_font_6x10_tf);const char* p=frame[row];
    for(unsigned col=0;col<3;++col){char cell[8]={};unsigned n=0;
      while(*p && *p!='|'){if(n<sizeof(cell)-1)cell[n++]=*p;++p;}
      if(*p=='|')++p;
      const int x=col*42,y=12+12*row;
      display.drawUTF8(x+3,y,cell);
      if(int((row-1)*3+col)==frameKeypadSelection)display.drawFrame(x,y-10,42,12);
    }
  } else {
    display.setFont(int(row)==frameLargeLine?u8g2_font_logisoso16_tf:u8g2_font_wqy12_t_gb2312);
    const int y=frameKeypadSelection>=0?(row==0?11:63):frameLargeLine>=0?(row==0?14:row==1?35:row==2?48:row==3?63:63):13+12*row;
    const int x=int(row)==frameLargeLine?(128-display.getUTF8Width(frame[row]))/2:0;
    display.drawUTF8(x,y,frame[row]);
  }
 }else display.updateDisplayArea(0,displayStage-5,16,1);
 ++displayStage;
}

bool initMpu() {
 invalidatePanelImu();
 for(uint8_t address=0x68;address<=0x69;++address){
  Wire.beginTransmission(address);Wire.write(0x75);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(address,uint8_t(1))!=1 || Wire.read()!=0x68)continue;
  bool ok=true;
  // 演示与本次采集固件一致；切回拼音恢复旧模型的量程和默认采样设置。
  const uint8_t regs[]={0x6B,0x6A,0x1A,0x19,0x1B,0x1C};
  const uint8_t demoValues[]={1,0,3,9,8,8};
  for(unsigned i=0;i<sizeof(regs);++i){Wire.beginTransmission(address);Wire.write(regs[i]);Wire.write(demoMode?demoValues[i]:uint8_t(0));if(Wire.endTransmission()!=0)ok=false;}
  if(ok){mpuAddress=address;mpuReady=true;lastMpuMs=millis();haveMpu=false;return true;}
 }
 mpuReady=false;return false;
}
bool readMpu(int16_t* raw) {
 Wire.beginTransmission(mpuAddress);Wire.write(0x3B);
 if(Wire.endTransmission(false)!=0 || Wire.requestFrom(mpuAddress,uint8_t(14))!=14)return false;
 for(unsigned i=0;i<7;++i){uint16_t hi=uint8_t(Wire.read()),lo=uint8_t(Wire.read());raw[i]=int16_t((hi<<8)|lo);}
 const float scale=demoMode?8192.f:16384.f;
 const float n=(float(raw[0])*raw[0]+float(raw[1])*raw[1]+float(raw[2])*raw[2])/(scale*scale);
 if(demoMode ? (n<.01f || n>48.f) : (n<.25f*.25f || n>3.5f*3.5f))return false;
 for(unsigned i=0;i<3;++i){panelImu[i]=raw[i]/scale;panelImu[i+3]=raw[i+4]/(demoMode?65.5f:131.f);}
 lastMpuMs=millis();haveMpu=true;publishPanelImu(lastMpuMs);return true;
}
bool mpuSampleCurrent(){return mpuReady && haveMpu && uint32_t(millis()-lastMpuMs)<=40;}
bool inputHealthy(){return mpuSampleCurrent();}
bool ready(){return inputHealthy() && (demoMode?demoRecognizer.ready():(phase==Phase::Idle && !requireQuiet));}
bool allowAction(char key) {
 if(!inputHealthy())return false;
 if(key!='x')return true;
 if(!::allowAssociationGesture())return false;
 const EmgView e=emgSnapshot();
 return samplingReady && e.warmed && e.worn && !e.uncertain && std::isfinite(e.newEnv) && e.newEnv>=0 && uint32_t(micros()-e.timeUs)<=20000;
}
void resetInput() {
 invalidatePanelImu();
 demoRecognizer.reset();demoAuthorized=-1;
 cancelGrip(millis());wristGate.reset();holdScroll.suspend();captureCount=0;authorizedKey=0;
 phase=Phase::Cooldown;phaseAt=millis();requireQuiet=true;quietPending=false;
 haveMpu=false;invalidateDisplay();
}
void setDemoMode(bool enabled) {
 panelModelValid=false;panelModelAt=0;panelModelLabel=-1;
 demoMode=enabled;gripCore.reset();holdScroll.reset();wristGate.reset();
 mpuReady=false;initMpu();mpuFailures=0;lastRetryMs=millis();resetInput();
 lastAction=enabled?"两动作演示模式":"原拼音模式";
 Serial.println(enabled?"【模式】三角形/Z演示，串口m切回原拼音。":"【模式】原拼音操作，串口m返回两动作演示。");
}
void emit(char key,const char* name) {
 lastAction=name;phase=Phase::Cooldown;phaseAt=millis();requireQuiet=true;quietPending=false;
 cancelGrip(millis());wristGate.reset();holdScroll.suspend();
 authorizedKey=key;
 Serial.print("【模型操作】");Serial.println(name);
 ::dispatchAction(key,true);
 authorizedKey=0;
}
void emitScroll(char key) {
 lastAction=key=='w'?"上翘连翻 松开停止":"下压连翻 松开停止";
 phase=Phase::Scroll;captureCount=0;cancelGrip(millis());wristGate.reset();authorizedKey=key;
 Serial.print("【连续选择】");Serial.println(key=='w'?"上翻":"下翻");
 ::dispatchAction(key,true);authorizedKey=0;
}
char actionKey(const char* label) {
 if(!std::strcmp(label,"up"))return 'w';
 if(!std::strcmp(label,"down"))return 's';
 if(!std::strcmp(label,"clockwise"))return 'f';
 if(!std::strcmp(label,"counterclockwise"))return 'b';
 if(!std::strcmp(label,"right_side"))return ' ';
 return 0;
}
const char* actionName(char key){switch(key){case 'w':return "上翻";case 's':return "下翻";case 'f':return "分隔符";case 'b':return "回退";case ' ':return "右侧翻确认";case 'x':return "握拳联想";}return "其他动作已忽略";}
const char* inputHint(const EmgView& e) {
 if(!mpuReady || !mpuSampleCurrent())return "等待姿态数据";
 if(demoMode)return demoRecognizer.hint;
 if(phase==Phase::Capture)return "正在识别手腕动作";
 if(phase==Phase::Scroll)return lastAction;
 if(phase==Phase::Cooldown)return lastAction;
 if(requireQuiet)return "手腕放松后再操作";
 if(!e.worn || e.uncertain)return "手腕可用 握拳请佩戴";
 return gripCore.hint;
}
void failedRead() {
 invalidatePanelImu();
 demoRecognizer.invalid("姿态异常 请稍候");demoAuthorized=-1;
 haveMpu=false;cancelGrip(millis());wristGate.reset();holdScroll.suspend();captureCount=0;requireQuiet=true;quietPending=false;phase=Phase::Idle;
 if(++mpuFailures>=3){mpuReady=false;lastRetryMs=millis();}
}
void loop() {
 const uint32_t now=millis();
 if(!mpuReady){if(uint32_t(now-lastRetryMs)>=1000){lastRetryMs=now;if(initMpu()){mpuFailures=0;resetInput();}}return;}
 if(uint32_t(now-lastControlMs)<10)return;
 if(demoMode){
  // 只消费传感器新样本；无新数据时不重复累计，读失败则丢弃窗口。
  Wire.beginTransmission(mpuAddress);Wire.write(0x3A);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(mpuAddress,uint8_t(1))!=1){failedRead();return;}
  if(!(Wire.read()&1)){if(uint32_t(now-lastMpuMs)>40)failedRead();return;}
 }
 const uint32_t gap=lastControlMs?uint32_t(now-lastControlMs):0;
 longestControlGap=std::max(longestControlGap,gap);
 const bool late=lastControlMs && gap>40;lastControlMs=now;
 if(late && phase==Phase::Capture){++captureGapDrops;failedRead();return;}
 int16_t raw[7];if(!readMpu(raw)){failedRead();return;}mpuFailures=0;
 if(demoMode){
  cancelGrip(now);captureCount=0;authorizedKey=0;
  if(late){demoRecognizer.invalid("采样中断 请重做");++captureGapDrops;return;}
  const uint32_t previousEvaluation=demoRecognizer.evaluationSequence;
  const int event=demoRecognizer.update(now,micros(),raw);
  if(previousEvaluation!=demoRecognizer.evaluationSequence){
   panelModelValid=true;panelModelAt=millis();panelModelLabel=demoRecognizer.lastLabel;
   panelModelMs=demoRecognizer.inferenceUs/1000.f;panelDistance=demoRecognizer.nearest;
   panelAlternative=demoRecognizer.alternative;panelDuration=demoRecognizer.durationMs;
  }
  if(event>=0){
   lastAction=event==0?"三角形：我想吃饭":"Z：你们好啊";
   Serial.print("【演示识别】");Serial.print(demo_model::LABELS[event]);
   Serial.print("，匹配距离=");Serial.print(demoRecognizer.nearest,4);
   Serial.print("，另一类距离=");Serial.print(demoRecognizer.alternative,4);
   Serial.print("，计算毫秒=");Serial.println(demoRecognizer.inferenceUs/1000.f,1);
   demoAuthorized=event;::dispatchDemoGesture(event);demoAuthorized=-1;
  }
  return; // 演示模式不让旧连翻、画圈或握拳抢先分发。
 }
 if(phase==Phase::Cooldown){cancelGrip(now);if(uint32_t(now-phaseAt)>=ACTION_COOLDOWN_MS){phase=Phase::Idle;wristGate.reset();}return;}
 if(!requireQuiet) {
  char scrollKey=holdScroll.update(now,raw,repeatIntervalMs);
  if(holdScroll.active()) {
   phase=Phase::Scroll;captureCount=0;cancelGrip(now);
   if(scrollKey)emitScroll(scrollKey);
   return;
  }
  if(phase==Phase::Scroll || holdScroll.justStopped){
   cancelGrip(now);wristGate.reset();captureCount=0;phase=Phase::Cooldown;phaseAt=now;requireQuiet=true;quietPending=false;return;
  }
 }
 if(phase==Phase::Capture){
  cancelGrip(now);
  const unsigned i=captureCount++;ax[i]=raw[0]/16384.f;ay[i]=raw[1]/16384.f;az[i]=raw[2]/16384.f;gx[i]=raw[4]/131.f;gy[i]=raw[5]/131.f;gz[i]=raw[6]/131.f;
  if(captureCount==WINDOW){
   static fusion_features::Sample samples[WINDOW];
   for(unsigned j=0;j<WINDOW;++j)samples[j]={ax[j],ay[j],az[j],gx[j],gy[j],gz[j],0};
   float f[40],p[wrist_model::CLASSES];fusion_features::imu(samples,WINDOW,f);wrist_model::predict(f,p);
   unsigned best=0;for(unsigned k=1;k<wrist_model::CLASSES;++k)if(p[k]>p[best])best=k;
   resultConf=p[best];char key=actionKey(wrist_model::LABELS[best]);
   if((key=='w' || key=='s') && holdScroll.continueModel(now,key))emitScroll(key);
   else if(key)emit(key,actionName(key));else {lastAction="其他动作已忽略";phase=Phase::Cooldown;phaseAt=now;requireQuiet=true;quietPending=false;}
  }
  return;
 }
 wristGate.update(now,raw);
 if(requireQuiet){
  cancelGrip(now);
  if(wristGate.decisionFresh(now) && wristGate.probability<.5f){if(!quietPending){quietPending=true;quietAt=now;}if(uint32_t(now-quietAt)>=200){requireQuiet=false;quietPending=false;}}
  else quietPending=false;
  return;
 }
 if(!::allowAssociationGesture())cancelGrip(now);
 else if(updateGrip(now,raw)){emit('x',actionName('x'));return;}
 if(wristGate.confirmed(now)){
  unsigned prefix=wristGate.seed(ax,ay,az,gx,gy,gz);
  if(prefix){cancelGrip(now);captureCount=prefix;phase=Phase::Capture;}
 }
 if(diagnostics)printGripDiagnostic();
}
void printHelp(){Serial.println("【演示】默认三角形播报我想吃饭，Z播报你们好啊；画完停住半秒。串口m切换演示/原拼音。");Serial.println("【操作】上/下翘连续选键组或组内字母，放回停止；右侧翻进入键组/输入字母；逆时针组内返回、组外删除；顺时针分隔；选组层握拳约1秒联想。串口w/s/f/b/空格/x同义，p查看状态，t切换诊断，c重新记录自然位。");}
void printStatus(){
 const EmgView e=emgSnapshot();
 Serial.print(demoMode?"【版本】1.2.2两动作演示，状态=":"【版本】1.2.2模型拼音-脑电无线调试，状态=");Serial.println(inputHint(e));
 Serial.print("【采样】任务=");Serial.print(samplingReady?"启动":"失败");Serial.print("，序号=");Serial.print(e.sequence);
 Serial.print("，预热=");Serial.print(e.warmed);Serial.print("，佩戴=");Serial.print(e.worn);Serial.print("，接触不稳=");Serial.print(e.uncertain);Serial.print("，肌电=");Serial.println(e.newEnv);
 Serial.print("【姿态】就绪=");Serial.print(mpuReady);Serial.print("，阶段=");Serial.print(unsigned(phase));Serial.print("，最大间隔毫秒=");Serial.print(longestControlGap);Serial.print("，中断丢窗=");Serial.println(captureGapDrops);
#if defined(ARDUINO_ARCH_ESP32)
 Serial.print("【运行】主循环剩余栈字节=");Serial.print(uxTaskGetStackHighWaterMark(nullptr));Serial.print("，空闲内存=");Serial.print(ESP.getFreeHeap());Serial.print("，重置按键电平=");Serial.println(digitalRead(RESET_BUTTON_PIN));
#endif
}
void receiveCommands(){unsigned budget=32;while(budget-- && Serial.available()){char c=char(Serial.read());if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(::handleApplicationCommand(c))continue;if(c=='c'){gripCore.reset();holdScroll.reset();resetInput();lastAction="自然位放松重新记录";}else if(c=='h'||c=='?')printHelp();else if(c=='p')printStatus();else if(c=='t')diagnostics=!diagnostics;}}
void clearSavedCalibration(){
#if defined(ARDUINO_ARCH_ESP32)
 Preferences preferences;preferences.begin("gesture-cal",false);preferences.clear();preferences.end();
#endif
}
void drawConfigWaitScreen(){queueScreen("等待WiFi配置","连接热点ESP32","手机打开","192.168.4.1");}
void drawScreen(){if(demoMode){queueDemoScreen("连接传感器","请稍候","连接");return;}queueScreen("模型拼音输入",inputHint(emgSnapshot()),samplingReady?"肌电采样运行":"肌电采样未启动",mpuReady?"姿态就绪":"姿态正在重试");}
void setup(){
 Serial.begin(115200);bluetoothSerial.begin(57600,SERIAL_8N1,16,17);extraSerial.begin(EXTRA_SERIAL_BAUDRATE,SERIAL_8N1,4,13);
 Serial.println("【启动】1.2.2模型拼音-脑电无线调试");
#if defined(ARDUINO_ARCH_ESP32)
 Serial.print("【启动】重启原因编号=");Serial.println(unsigned(esp_reset_reason()));
#endif
 for(uint8_t pin:LED_PINS){digitalWrite(pin,LOW);pinMode(pin,OUTPUT);}pinMode(DETECT_PIN,INPUT);analogReadResolution(10);analogSetPinAttenuation(EMG_PIN,ADC_11db);sEMG.begin();
 samplingReady=xTaskCreatePinnedToCore(emgSampleTask,"EMG500Hz",4096,nullptr,2,nullptr,1)==pdPASS;
 Serial.println(samplingReady?"【采样】肌电500Hz任务创建成功，预热1.5秒后按佩戴及6/9/12旧包络控制三灯。":"【错误】肌电采样任务创建失败，三灯及握拳不可用。");
 Wire.begin(21,22);Wire.setClock(400000);Wire.setTimeOut(10);display.setBusClock(400000);
 for(uint8_t address=0x3C;address<=0x3D;++address){Wire.beginTransmission(address);if(Wire.endTransmission()!=0)continue;display.setI2CAddress(address<<1);oledReady=display.begin();break;}
 if(oledReady)display.enableUTF8Print();Wire.setClock(400000);Wire.setTimeOut(10);initMpu();printHelp();
}
} // namespace gesture
