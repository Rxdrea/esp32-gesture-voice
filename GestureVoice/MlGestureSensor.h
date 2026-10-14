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

namespace gesture {
float resultConf=0;
#include "EmgRecognition.h"
constexpr uint8_t RESET_BUTTON_PIN=33;
constexpr uint32_t RESET_HOLD_MS=1000;
HardwareSerial bluetoothSerial(2),extraSerial(1);
constexpr uint32_t EXTRA_SERIAL_BAUDRATE=9600;
uint8_t brainSignalQuality=0,brainAttention=0,brainMeditation=0;
bool brainDataReady=false;
// 继续兼容HC-04送来的旧32字节脑电数值包；仅保存数值。
class BrainParser {
 unsigned state_=0,index_=0,length_=0,sum_=0;uint8_t data_[32]={};
 public:
 void consume(uint8_t b) {
  switch(state_) {
   case 0:if(b==0xAA)state_=1;break;
   case 1:state_=b==0xAA?2:0;break;
   case 2:length_=b;index_=sum_=0;state_=length_==32?3:0;break;
   case 3:data_[index_++]=b;sum_+=b;if(index_==length_)state_=4;break;
   case 4:if(b==uint8_t(~sum_)){brainSignalQuality=data_[1];brainAttention=data_[29];brainMeditation=data_[31];brainDataReady=true;}state_=0;break;
  }
 }
} brainParser;
void readBrainData(){unsigned budget=128;while(budget-- && bluetoothSerial.available())brainParser.consume(uint8_t(bluetoothSerial.read()));}

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0,U8X8_PIN_NONE,22,21);
bool samplingReady=false,oledReady=false,mpuReady=false;
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
void queueScreen(const char* a,const char* b,const char* c,const char* d,const char* e=nullptr,int large=-1) {
 const char* lines[]={a,b,c,d,e};char normalized[5][96]={};bool changed=largeLine!=large;
 for(unsigned i=0;i<5;++i){
  const char* text=lines[i]?lines[i]:"";size_t n=std::min(std::strlen(text),sizeof(normalized[i])-1);
  // 从截断处退回完整UTF-8字符边界，比较的也是最终实际显示内容。
  while(n && (uint8_t(text[n])&0xC0)==0x80)--n;
  std::memcpy(normalized[i],text,n);
  if(std::strcmp(screen[i],normalized[i])!=0)changed=true;
 }
 if(!changed)return;
 std::memcpy(screen,normalized,sizeof(screen));
 largeLine=large;++screenVersion;
}
void invalidateDisplay(){displayStage=13;frameVersion=screenVersion-1;}
void serviceDisplay() {
 if(!oledReady || uint32_t(millis()-lastSliceMs)<10)return;
 lastSliceMs=millis();
 if(displayStage>=13){if(frameVersion==screenVersion)return;std::memcpy(frame,screen,sizeof(frame));frameLargeLine=largeLine;frameVersion=screenVersion;display.clearBuffer();display.setFontPosBaseline();displayStage=0;}
 if(displayStage<5){
  const unsigned row=displayStage;
  display.setFont(int(row)==frameLargeLine?u8g2_font_logisoso16_tf:u8g2_font_wqy12_t_gb2312);
  const int y=frameLargeLine>=0?(row==0?14:row==1?35:row==2?48:row==3?63:63):13+12*row;
  const int x=int(row)==frameLargeLine?(128-display.getUTF8Width(frame[row]))/2:0;
  display.drawUTF8(x,y,frame[row]);
 }else display.updateDisplayArea(0,displayStage-5,16,1);
 ++displayStage;
}

bool initMpu() {
 for(uint8_t address=0x68;address<=0x69;++address){
  Wire.beginTransmission(address);Wire.write(0x75);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(address,uint8_t(1))!=1 || Wire.read()!=0x68)continue;
  bool ok=true;
  for(uint8_t reg:{uint8_t(0x6B),uint8_t(0x1B),uint8_t(0x1C)}){Wire.beginTransmission(address);Wire.write(reg);Wire.write(0);if(Wire.endTransmission()!=0)ok=false;}
  if(ok){mpuAddress=address;mpuReady=true;return true;}
 }
 mpuReady=false;return false;
}
bool readMpu(int16_t* raw) {
 Wire.beginTransmission(mpuAddress);Wire.write(0x3B);
 if(Wire.endTransmission(false)!=0 || Wire.requestFrom(mpuAddress,uint8_t(14))!=14)return false;
 for(unsigned i=0;i<7;++i){uint16_t hi=uint8_t(Wire.read()),lo=uint8_t(Wire.read());raw[i]=int16_t((hi<<8)|lo);}
 const float n=(float(raw[0])*raw[0]+float(raw[1])*raw[1]+float(raw[2])*raw[2])/(16384.f*16384.f);
 if(n<.25f*.25f || n>3.5f*3.5f)return false;
 lastMpuMs=millis();haveMpu=true;return true;
}
bool mpuSampleCurrent(){return mpuReady && haveMpu && uint32_t(millis()-lastMpuMs)<=40;}
bool inputHealthy(){return mpuSampleCurrent();}
bool ready(){return phase==Phase::Idle && !requireQuiet && inputHealthy();}
bool allowAction(char key) {
 if(!inputHealthy())return false;
 if(key!='x')return true;
 const EmgView e=emgSnapshot();
 return samplingReady && e.warmed && e.worn && !e.uncertain && std::isfinite(e.newEnv) && e.newEnv>=0 && uint32_t(micros()-e.timeUs)<=20000;
}
void resetInput() {
 cancelGrip(millis());wristGate.reset();holdScroll.suspend();captureCount=0;authorizedKey=0;
 phase=Phase::Cooldown;phaseAt=millis();requireQuiet=true;quietPending=false;
 haveMpu=false;invalidateDisplay();
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
 if(phase==Phase::Capture)return "正在识别手腕动作";
 if(phase==Phase::Scroll)return lastAction;
 if(phase==Phase::Cooldown)return lastAction;
 if(requireQuiet)return "手腕放松后再操作";
 if(!e.worn || e.uncertain)return "手腕可用 握拳请佩戴";
 return gripCore.hint;
}
void failedRead() {
 haveMpu=false;cancelGrip(millis());wristGate.reset();holdScroll.suspend();captureCount=0;requireQuiet=true;quietPending=false;phase=Phase::Idle;
 if(++mpuFailures>=3){mpuReady=false;lastRetryMs=millis();}
}
void loop() {
 readBrainData();const uint32_t now=millis();
 if(!mpuReady){if(uint32_t(now-lastRetryMs)>=1000){lastRetryMs=now;if(initMpu()){mpuFailures=0;resetInput();}}return;}
 if(uint32_t(now-lastControlMs)<10)return;
 const uint32_t gap=lastControlMs?uint32_t(now-lastControlMs):0;
 longestControlGap=std::max(longestControlGap,gap);
 const bool late=lastControlMs && gap>40;lastControlMs=now;
 if(late && phase==Phase::Capture){++captureGapDrops;failedRead();return;}
 int16_t raw[7];if(!readMpu(raw)){failedRead();return;}mpuFailures=0;
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
 if(updateGrip(now,raw)){emit('x',actionName('x'));return;}
 if(wristGate.confirmed(now)){
  unsigned prefix=wristGate.seed(ax,ay,az,gx,gy,gz);
  if(prefix){cancelGrip(now);captureCount=prefix;phase=Phase::Capture;}
 }
 if(diagnostics)printGripDiagnostic();
}
void printHelp(){Serial.println("【操作】上/下翘保持连续选择，放回停止；顺时针分隔，逆时针回退，右侧翻确认，握拳约1秒联想。串口w/s/f/b/空格/x同义，p查看状态，t切换诊断，c重新记录自然位。");}
void printStatus(){
 const EmgView e=emgSnapshot();
 Serial.print("【版本】1.1.2模型拼音-连续选择容错20261014，状态=");Serial.println(inputHint(e));
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
void drawScreen(){queueScreen("模型拼音输入",inputHint(emgSnapshot()),samplingReady?"肌电采样运行":"肌电采样未启动",mpuReady?"姿态就绪":"姿态正在重试");}
void setup(){
 Serial.begin(115200);bluetoothSerial.begin(57600,SERIAL_8N1,16,17);extraSerial.begin(EXTRA_SERIAL_BAUDRATE,SERIAL_8N1,4,13);
 Serial.println("【启动】1.1.2模型拼音-连续选择容错20261014");
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
