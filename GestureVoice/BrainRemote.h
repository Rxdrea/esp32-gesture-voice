#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "BrainProtocol.h"
#include "BrainDecision.h"

namespace brain_remote {
constexpr uint32_t STALE_MS=3000, SEND_MS=750;
struct Snapshot {
 uint8_t quality=200,attention=0,meditation=0,command=0,channel=0,pending=0,confirmCount=0;
 bool haveQuality=false,haveAttention=false,haveMeditation=false,wirelessReady=false,decisionReady=false;
 uint32_t qualityMs=0,attentionMs=0,meditationMs=0,sequence=0,submitted=0,sendErrors=0,parseErrors=0;
};
Snapshot shared,working;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
HardwareSerial* input=nullptr;
bool taskReady=false;
uint32_t session=0,lastSendMs=0,lastInitMs=0;
bool forceSend=false;
eeg_decision::Decision decision;

void publish(){working.pending=decision.pending();working.confirmCount=decision.count();portENTER_CRITICAL(&mux);shared=working;portEXIT_CRITICAL(&mux);}
Snapshot snapshot(){portENTER_CRITICAL(&mux);Snapshot result=shared;portEXIT_CRITICAL(&mux);return result;}
uint8_t flags(const Snapshot& s,uint32_t now) {
 uint8_t f=0;
 if(s.haveAttention && uint32_t(now-s.attentionMs)<=STALE_MS)f|=EEG_FRESH;
 if(s.haveQuality && s.quality==0 && uint32_t(now-s.qualityMs)<=STALE_MS)f|=EEG_CONTACT;
 if(s.decisionReady)f|=EEG_DECISION;
 return f;
}
const char* commandText(uint8_t command){return command==uint8_t(EegCommand::Start)?"on（启动）":command==uint8_t(EegCommand::Stop)?"off（停止）":"等待判断";}

void parse(const uint8_t* data,size_t size) {
 uint8_t quality=working.quality,attention=working.attention,meditation=working.meditation;
 bool haveQuality=false,haveAttention=false,haveMeditation=false;
 size_t pos=0;
 while(pos<size) {
  unsigned extended=0;while(pos<size && data[pos]==0x55){++extended;++pos;}
  if(pos>=size){++working.parseErrors;decision.invalidate();return;}
  uint8_t code=data[pos++];size_t length=1;
  if(code>=0x80){if(pos>=size){++working.parseErrors;decision.invalidate();return;}length=data[pos++];}
  if(length>size-pos){++working.parseErrors;decision.invalidate();return;}
  if(!extended && length==1) {
   if(code==2){quality=data[pos];haveQuality=true;}
   else if(code==4){if(data[pos]>100){++working.parseErrors;decision.invalidate();return;}attention=data[pos];haveAttention=true;}
   else if(code==5){if(data[pos]>100){++working.parseErrors;decision.invalidate();return;}meditation=data[pos];haveMeditation=true;}
  }
  pos+=length;
 }
 const uint32_t now=millis();const uint8_t oldCommand=working.command;
 if(haveQuality){working.quality=quality;working.haveQuality=true;working.qualityMs=now;if(quality!=0){working.decisionReady=false;decision.invalidate();}}
 if(haveMeditation){working.meditation=meditation;working.haveMeditation=true;working.meditationMs=now;}
 if(haveAttention) {
  working.attention=attention;working.haveAttention=true;working.attentionMs=now;
  if(working.haveQuality && working.quality==0 && uint32_t(now-working.qualityMs)<=STALE_MS) {
   working.decisionReady=true;
   decision.accept(now,attention,true);
   working.command=decision.state();
  } else {working.decisionReady=false;decision.invalidate();}
 }
 if(working.command!=oldCommand)forceSend=true;
 if(haveQuality || haveAttention || haveMeditation)publish();
 // 每个校验通过且携带新专注度的包只打印一次，不反复打印旧值。
 if(haveAttention) {
  char line[192];
  snprintf(line,sizeof(line),"【脑电数据】专注度=%u | 放松度=%u | 信号质量=%u | 判断=%s | 确认=%u/2",
           unsigned(working.attention),unsigned(working.meditation),unsigned(working.quality),
           working.decisionReady?commandText(working.command):"数据无效或佩戴异常",unsigned(decision.count()));
  Serial.println(line);
 }
}
class Parser {
 unsigned stage_=0,length_=0,index_=0;uint8_t sum_=0,payload_[169]={};
 uint32_t lastByteMs_=0;
public:
 void consume(uint8_t value) {
  uint32_t now=millis();if(stage_ && uint32_t(now-lastByteMs_)>200){stage_=0;decision.invalidate();}lastByteMs_=now;
  switch(stage_) {
   case 0:if(value==0xAA)stage_=1;break;
   case 1:stage_=value==0xAA?2:0;break;
   case 2:
    if(value==0xAA)break;
    if(value>sizeof(payload_)){stage_=0;++working.parseErrors;decision.invalidate();break;}
    length_=value;index_=sum_=0;stage_=length_?3:4;break;
   case 3:payload_[index_++]=value;sum_=uint8_t(sum_+value);if(index_==length_)stage_=4;break;
   case 4:stage_=0;if(value==uint8_t(~sum_))parse(payload_,length_);else {++working.parseErrors;decision.invalidate();}break;
  }
 }
} parser;

void serviceRadio(uint32_t now) {
 if((flags(working,now)&(EEG_FRESH|EEG_CONTACT))!=(EEG_FRESH|EEG_CONTACT)){decision.invalidate();working.decisionReady=false;}
 uint8_t channel=0;wifi_second_chan_t secondary;
 if(esp_wifi_get_channel(&channel,&secondary)!=ESP_OK)return;
 working.channel=channel;
 if(!working.wirelessReady) {
  if(uint32_t(now-lastInitMs)<1000)return;lastInitMs=now;
  if(esp_now_init()!=ESP_OK)return;
  wifi_mode_t mode;
  if(esp_wifi_get_mode(&mode)!=ESP_OK){esp_now_deinit();return;}
  esp_now_peer_info_t peer={};memset(peer.peer_addr,255,6);
  // 信道0表示使用WiFi当前信道，不设置固定信道、不调用WiFi.begin或mode。
  peer.channel=0;peer.ifidx=mode==WIFI_MODE_AP?WIFI_IF_AP:WIFI_IF_STA;peer.encrypt=false;
  if(esp_now_add_peer(&peer)!=ESP_OK){esp_now_deinit();return;}
  working.wirelessReady=true;forceSend=true;
 }
 if(!forceSend && uint32_t(now-lastSendMs)<SEND_MS)return;
 forceSend=false;lastSendMs=now;
 EegPacket p={};
 p.magic=EEG_MAGIC;p.version=EEG_VERSION;p.size=sizeof(p);
 p.command=working.command;p.quality=working.quality;p.attention=working.attention;
 p.meditation=working.meditation;p.flags=flags(working,now);p.channel=channel;
 p.session=session;p.sequence=++working.sequence;
 p.sampleAgeMs=working.haveAttention?uint32_t(now-working.attentionMs):UINT32_MAX;
 const uint8_t broadcast[6]={255,255,255,255,255,255};
 if(esp_now_send(broadcast,reinterpret_cast<const uint8_t*>(&p),sizeof(p))==ESP_OK)++working.submitted;
 else ++working.sendErrors;
 publish();
}
void task(void*) {
 for(;;) {
  unsigned budget=128;
  while(budget-- && input->available())parser.consume(uint8_t(input->read()));
  serviceRadio(millis());
  vTaskDelay(pdMS_TO_TICKS(2));
 }
}
void begin(HardwareSerial& serial) {
 input=&serial;session=esp_random();
 taskReady=xTaskCreatePinnedToCore(task,"EEG-UART-ESPNow",4096,nullptr,1,nullptr,0)==pdPASS;
 Serial.println(taskReady?"【脑电】后台接收及无线任务启动，HC-04为16/17、57600。":"【脑电错误】后台任务创建失败。");
}
// 新脑电包自动打印一行；手动e另查询无线状态，不打印任何私有配置。
void printStatus() {
 const Snapshot s=snapshot();uint8_t f=flags(s,millis());
 Serial.print("【脑电】专注度=");Serial.print(s.attention);Serial.print("，放松度=");Serial.print(s.meditation);
 Serial.print("，信号质量=");Serial.print(s.quality);Serial.print("，判断=");
 Serial.println((f&(EEG_FRESH|EEG_CONTACT|EEG_DECISION))!=(EEG_FRESH|EEG_CONTACT|EEG_DECISION)?"数据未到、过期或佩戴异常":commandText(s.command));
 Serial.print("【脑电无线】任务=");Serial.print(taskReady);Serial.print("，启动=");Serial.print(s.wirelessReady);
 Serial.print("，跟随信道=");Serial.print(s.channel);Serial.print("，提交次数=");Serial.print(s.submitted);
 Serial.print("，提交失败=");Serial.print(s.sendErrors);Serial.print("，坏包=");Serial.println(s.parseErrors);
 Serial.print("【脑电确认】候选=");Serial.print(commandText(s.pending));Serial.print("，新有效包=");Serial.print(s.confirmCount);Serial.println("/2");
 Serial.println("【规则】>70连续两包启动，<50连续两包停止，50至70保持；平滑默认关闭。");
 Serial.println("【说明】提交成功不代表第二块已收到，以接收端串口为准。");
}
}
