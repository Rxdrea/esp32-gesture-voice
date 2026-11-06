#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "BrainProtocol.h"

struct Incoming {EegPacket packet;uint8_t mac[6];uint8_t receivedChannel;};
QueueHandle_t inbox=nullptr;
bool ready=false,paired=false,online=false,validBefore=false,haveDataStatus=false;
uint8_t source[6]={},currentChannel=1,lastCommand=0;
uint32_t lastReceive=0,lastHop=0,lastSession=0,lastSequence=0;
constexpr uint32_t HOP_MS=1000, LOST_MS=5000;
constexpr uint8_t PIN_25=25, LED_PIN=2, PIN_26=26;
// 阀门软启动：20kHz PWM，0.3秒内逐步升到满占空比，削掉开阀冲击电流防止掉电重启。
constexpr uint32_t PWM_FREQ=20000;
constexpr uint8_t PWM_BITS=8;
constexpr int RAMP_MS=300, HOLD_DUTY=255;

void softOn(uint8_t pin) {
 for(int i=1;i<=50;i++){ledcWrite(pin,HOLD_DUTY*i/50);delay(RAMP_MS/50);}
}

// 切换时先撤销全部输出，再软启动目标，避免25与26同时为高。
void applyCommand(uint8_t command) {
 ledcWrite(PIN_25,0);digitalWrite(LED_PIN,LOW);ledcWrite(PIN_26,0);
 if(command==uint8_t(EegCommand::Start)) {
  digitalWrite(LED_PIN,HIGH);softOn(PIN_26);
 } else if(command==uint8_t(EegCommand::Stop)) {
  softOn(PIN_25);
 }
}

void onReceive(const esp_now_recv_info_t* info,const uint8_t* data,int length) {
 if(!info || !data || !inbox || length!=sizeof(EegPacket))return;
 Incoming item;memcpy(&item.packet,data,sizeof(item.packet));const EegPacket& p=item.packet;
 if(p.magic!=EEG_MAGIC || p.version!=EEG_VERSION || p.size!=sizeof(p) || p.command>2 ||
    p.attention>100 || p.meditation>100 || p.channel<1 || p.channel>13)return;
 memcpy(item.mac,info->src_addr,6);
 item.receivedChannel=info->rx_ctrl?info->rx_ctrl->channel:item.packet.channel;
 xQueueOverwrite(inbox,&item);
}
void setChannel(uint8_t channel) {
 if(esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)==ESP_OK)currentChannel=channel;
 lastHop=millis();
}
void setup() {
 // 上电先保持三路低电平，等待首个有效确认命令。
 digitalWrite(PIN_25,LOW);digitalWrite(LED_PIN,LOW);digitalWrite(PIN_26,LOW);
 pinMode(PIN_25,OUTPUT);pinMode(LED_PIN,OUTPUT);pinMode(PIN_26,OUTPUT);
 ledcAttach(PIN_25,PWM_FREQ,PWM_BITS);ledcAttach(PIN_26,PWM_FREQ,PWM_BITS);
 ledcWrite(PIN_25,0);ledcWrite(PIN_26,0);
 Serial.begin(115200);
 inbox=xQueueCreate(1,sizeof(Incoming));
 WiFi.mode(WIFI_STA);WiFi.setSleep(false);
 if(!inbox || esp_wifi_set_country_code("CN",false)!=ESP_OK) {
  Serial.println("【错误】接收队列或无线地区初始化失败。");return;
 }
 setChannel(1);
 if(esp_now_init()!=ESP_OK || esp_now_register_recv_cb(onReceive)!=ESP_OK) {
  Serial.println("【错误】ESP-NOW初始化失败。");return;
 }
 ready=true;
 Serial.println("【1.2.3接收端】自动搜索信道1至13，无需输入热点密码；阀门软启动已开启。");
 Serial.println("【调试】on：26高、2高、25低；off：25高、2低、26低；上电三路低。");
}
void loop() {
 if(!ready){delay(10);return;}
 const uint32_t now=millis();
 Incoming item;
 if(xQueueReceive(inbox,&item,0)==pdTRUE) {
  const EegPacket& p=item.packet;
  if(!paired || memcmp(source,item.mac,6)==0) {
   if(!paired){memcpy(source,item.mac,6);paired=true;}
   if(!online || p.session!=lastSession || int32_t(p.sequence-lastSequence)>0) {
    bool newSession=online && p.session!=lastSession;
    // 搜索切换瞬间到达的包也锁定其实际接收信道。
    if(item.receivedChannel>=1 && item.receivedChannel<=13 && item.receivedChannel!=currentChannel)setChannel(item.receivedChannel);
    if(!online) {
     Serial.print("【连接】已找到第一块ESP32，信道=");Serial.println(currentChannel);
    }
    if(newSession)Serial.println("【连接】第一块已重新启动。");
    online=true;lastReceive=now;lastSession=p.session;lastSequence=p.sequence;
    const bool valid=(p.flags&(EEG_FRESH|EEG_CONTACT|EEG_DECISION))==(EEG_FRESH|EEG_CONTACT|EEG_DECISION) &&
                     p.sampleAgeMs<=3000;
    if(!valid) {
     if(!haveDataStatus || validBefore) {
      Serial.println("【脑电】无有效数据或佩戴异常，暂停指令判断。");
      // 单独用validBefore区分首次异常，防止每个心跳都打印。
     }
    } else if(p.command && p.command!=lastCommand) {
     Serial.print(p.command==uint8_t(EegCommand::Start)?"【执行】on：连续两包专注度大于70":"【执行】off：连续两包专注度小于50");
     Serial.print(" | 专注度=");Serial.print(p.attention);
     Serial.print(" | 放松度=");Serial.println(p.meditation);
     applyCommand(p.command);
     Serial.println(p.command==uint8_t(EegCommand::Start)?"【输出】GPIO25=低，GPIO2=高，GPIO26=高":"【输出】GPIO25=高，GPIO2=低，GPIO26=低");
     lastCommand=p.command;
    } else if(valid && !validBefore && lastCommand) {
     Serial.println("【脑电】数据恢复，相同状态不重复执行。");
    }
    validBefore=valid;haveDataStatus=true;
   }
  }
 }
 if(online && uint32_t(now-lastReceive)>LOST_MS) {
  online=false;validBefore=false;haveDataStatus=false;lastHop=0;
  Serial.println("【断开】超过5秒未收到数据，重新搜索信道；不执行旧指令。");
 }
 if(!online && uint32_t(now-lastHop)>=HOP_MS)setChannel(currentChannel>=13?1:currentChannel+1);
 delay(1);
}