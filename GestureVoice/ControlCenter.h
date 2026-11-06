#pragma once
// 1.2.2：HTTP只在ESP-IDF自带后台任务处理，主循环只交换固定快照与命令。
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#pragma push_macro("HTTP_ANY")
#undef HTTP_ANY
#include <esp_http_server.h>
#undef HTTP_ANY
#pragma pop_macro("HTTP_ANY")
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "ControlCenterPage.h"

namespace control_center {
enum class Action : uint8_t { Oled, Speaker, Volume, Mode };
struct Command { Action action; int value; uint32_t id,createdMs; };
struct Runtime {
 uint32_t capturedMs=0,imuMs=0,controlGapMs=0,captureDrops=0,modelAtMs=0;
 bool modelValid=false;int8_t modelLabel=-1;
 bool haveImu=false,mpuReady=false,demo=true,busy=false,oledReady=false,oledEnabled=true,speakerEnabled=true,audioActive=false;
 uint8_t volume=40,layout=0;
 float imu[6]={},modelMs=0,matchDistance=0,alternativeDistance=0,durationMs=0;
 char hint[96]={},action[96]={},sentence[256]={},pinyin[128]={},lines[5][96]={};
};
struct Reply { uint32_t id=0; uint8_t status=0; char message[96]={}; };
Runtime shared;
Reply reply;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t commands=nullptr;
httpd_handle_t server=nullptr;
bool taskReady=false;
uint32_t nextId=0;
void (*fillLive)(JsonObject)=nullptr;
// 以下输出设置只有主循环写；网络线程读取的都是Runtime副本。
bool speakerEnabled=true,audioActive=false;
uint8_t volume=40;
char sentence[256]={};

void textCopy(char* dest,size_t size,const char* source){
 if(!size)return;
 const char* s=source?source:"";size_t n=std::min(strlen(s),size-1);
 while(n && (uint8_t(s[n])&0xC0)==0x80)--n;
 memcpy(dest,s,n);dest[n]=0;
}
void publish(const Runtime& value){portENTER_CRITICAL(&mux);shared=value;portEXIT_CRITICAL(&mux);}
Runtime snapshot(){portENTER_CRITICAL(&mux);Runtime value=shared;portEXIT_CRITICAL(&mux);return value;}
Reply replySnapshot(){portENTER_CRITICAL(&mux);Reply value=reply;portEXIT_CRITICAL(&mux);return value;}
void complete(uint32_t id,bool ok,const char* message){
 portENTER_CRITICAL(&mux);
 if(reply.id==id){reply.status=ok?2:3;textCopy(reply.message,sizeof(reply.message),message);}
 portEXIT_CRITICAL(&mux);
}
bool take(Command& command){return commands && xQueueReceive(commands,&command,0)==pdTRUE;}

esp_err_t jsonError(httpd_req_t* req,const char* status,const char* message){
 JsonDocument doc;doc["ok"]=false;doc["error"]=message;
 char out[256];size_t length=serializeJson(doc,out,sizeof(out));
 httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json; charset=utf-8");
 httpd_resp_set_hdr(req,"Cache-Control","no-store");
 return httpd_resp_send(req,out,length);
}
esp_err_t pageHandler(httpd_req_t* req){
 httpd_resp_set_type(req,"text/html; charset=utf-8");
 httpd_resp_set_hdr(req,"Cache-Control","no-cache");
 httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
 httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'none'");
 return httpd_resp_send(req,CONTROL_CENTER_HTML,HTTPD_RESP_USE_STRLEN);
}
esp_err_t stateHandler(httpd_req_t* req){
 // HTTP服务只有自己的任务访问此缓冲；不在锁内构建JSON或发送网络数据。
 static char out[4096];
 JsonDocument doc;
 const Runtime s=snapshot();const Reply ack=replySnapshot();const uint32_t now=millis();
 doc["version"]="1.2.2";doc["uptime_ms"]=now;
 doc["state_age_ms"]=s.capturedMs?uint32_t(now-s.capturedMs):UINT32_MAX;
 doc["mode"]=s.demo?"demo":"pinyin";doc["busy"]=s.busy;
 doc["hint"]=s.hint;doc["action"]=s.action;doc["sentence"]=s.sentence;doc["pinyin"]=s.pinyin;
 JsonObject device=doc["device"].to<JsonObject>();
 device["wifi"]=WiFi.status()==WL_CONNECTED;device["ip"]=WiFi.localIP().toString();
 device["rssi"]=WiFi.RSSI();device["heap"]=ESP.getFreeHeap();
 JsonObject screen=doc["display"].to<JsonObject>();
 screen["enabled"]=s.oledEnabled;screen["ready"]=s.oledReady;screen["layout"]=s.layout;
 JsonArray lines=screen["lines"].to<JsonArray>();for(const auto& line:s.lines)lines.add(line);
 JsonObject audio=doc["audio"].to<JsonObject>();audio["enabled"]=s.speakerEnabled;audio["volume"]=s.volume;audio["active"]=s.audioActive;
 JsonObject diagnostics=doc["diagnostics"].to<JsonObject>();
 diagnostics["control_gap_ms"]=s.controlGapMs;diagnostics["capture_drops"]=s.captureDrops;
 diagnostics["model_valid"]=s.modelValid;diagnostics["model_mode"]="demo";
 diagnostics["model_age_ms"]=s.modelValid?uint32_t(now-s.modelAtMs):UINT32_MAX;
 diagnostics["model_result"]=!s.modelValid?"none":s.modelLabel==0?"triangle":s.modelLabel==1?"z":"rejected";
 if(s.modelValid){
  diagnostics["model_ms"]=s.modelMs;diagnostics["match_distance"]=s.matchDistance;
  diagnostics["alternative_distance"]=s.alternativeDistance;diagnostics["duration_ms"]=s.durationMs;
 }else{diagnostics["model_ms"]=nullptr;diagnostics["match_distance"]=nullptr;diagnostics["alternative_distance"]=nullptr;diagnostics["duration_ms"]=nullptr;}
 JsonObject control=doc["control"].to<JsonObject>();control["id"]=ack.id;
 control["status"]=ack.status==1?"queued":ack.status==2?"applied":ack.status==3?"rejected":"none";
 control["message"]=ack.message;
 JsonObject caps=doc["capabilities"].to<JsonObject>();caps["oled"]=s.oledReady;caps["speaker"]=true;caps["volume"]=true;caps["mode"]=true;
 if(fillLive)fillLive(doc.as<JsonObject>());
 if(doc.overflowed() || measureJson(doc)>=sizeof(out))return jsonError(req,"500 Internal Server Error","状态数据超出缓冲");
 const size_t length=serializeJson(doc,out,sizeof(out));
 httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
 return httpd_resp_send(req,out,length);
}
esp_err_t controlHandler(httpd_req_t* req){
 // 仅接受本站页面的JSON控制；不增加远程GPIO、串口透传或任意代码入口。
 char origin[96]={},contentType[64]={};
 if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK)return jsonError(req,"403 Forbidden","请从设备本地页面操作");
 const String expected=String("http://")+WiFi.localIP().toString();
 if(expected!=origin && expected+":80"!=origin)return jsonError(req,"403 Forbidden","跨站控制已拒绝");
 if(httpd_req_get_hdr_value_str(req,"Content-Type",contentType,sizeof(contentType))!=ESP_OK || strncmp(contentType,"application/json",16)!=0)return jsonError(req,"400 Bad Request","仅接受JSON控制");
 if(req->content_len<=0 || req->content_len>256)return jsonError(req,"400 Bad Request","控制内容长度无效");
 char body[257]={};int received=0;
 while(received<req->content_len){int n=httpd_req_recv(req,body+received,req->content_len-received);if(n<=0)return ESP_FAIL;received+=n;}
 JsonDocument doc;if(deserializeJson(doc,body,received))return jsonError(req,"400 Bad Request","控制格式无效");
 if(!doc.is<JsonObject>() || doc.size()!=2 || !doc["action"].is<const char*>())return jsonError(req,"400 Bad Request","控制字段无效");
 const Runtime s=snapshot();const Reply ack=replySnapshot();const uint32_t now=millis();
 if(s.busy)return jsonError(req,"423 Locked","设备正在处理，请稍后操作");
 if(!s.capturedMs || uint32_t(now-s.capturedMs)>1500)return jsonError(req,"503 Service Unavailable","主循环状态尚未更新");
 if(ack.status==1)return jsonError(req,"409 Conflict","上一条控制尚未完成");
 const char* action=doc["action"];Command command={};
 if(!strcmp(action,"oled") || !strcmp(action,"speaker")){
  if(!doc["value"].is<bool>())return jsonError(req,"400 Bad Request","开关值必须为布尔值");
  command.action=!strcmp(action,"oled")?Action::Oled:Action::Speaker;command.value=doc["value"].as<bool>();
  if(command.action==Action::Oled && !s.oledReady)return jsonError(req,"409 Conflict","OLED尚未就绪");
 }else if(!strcmp(action,"volume")){
  if(!doc["value"].is<int>() || doc["value"].as<int>()<0 || doc["value"].as<int>()>100)return jsonError(req,"400 Bad Request","音量必须为0到100的整数");
  command.action=Action::Volume;command.value=doc["value"].as<int>();
 }else if(!strcmp(action,"mode")){
  const char* value=doc["value"].is<const char*>()?doc["value"].as<const char*>():"";
  if(strcmp(value,"demo") && strcmp(value,"pinyin"))return jsonError(req,"400 Bad Request","工作模式无效");
  command.action=Action::Mode;command.value=!strcmp(value,"demo");
 }else return jsonError(req,"400 Bad Request","不支持此控制");
 if(!commands)return jsonError(req,"503 Service Unavailable","控制队列尚未启动");
 command.id=++nextId;if(!command.id)command.id=++nextId;command.createdMs=now;
 portENTER_CRITICAL(&mux);reply.id=command.id;reply.status=1;textCopy(reply.message,sizeof(reply.message),"等待设备执行");portEXIT_CRITICAL(&mux);
 if(xQueueSend(commands,&command,0)!=pdTRUE){complete(command.id,false,"控制队列已满");return jsonError(req,"503 Service Unavailable","控制队列已满");}
 JsonDocument accepted;accepted["ok"]=true;accepted["id"]=command.id;accepted["status"]="queued";char out[128];size_t len=serializeJson(accepted,out,sizeof(out));
 httpd_resp_set_status(req,"202 Accepted");httpd_resp_set_type(req,"application/json; charset=utf-8");httpd_resp_set_hdr(req,"Cache-Control","no-store");
 return httpd_resp_send(req,out,len);
}
void monitor(void*){
 for(;;){
  if(WiFi.status()==WL_CONNECTED){
   if(!server){
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.core_id=0;config.task_priority=1;
    config.stack_size=8192;config.max_open_sockets=3;config.max_uri_handlers=4;
    config.lru_purge_enable=true;config.recv_wait_timeout=1;config.send_wait_timeout=2;
    if(httpd_start(&server,&config)==ESP_OK){
     const httpd_uri_t page={.uri="/",.method=HTTP_GET,.handler=pageHandler,.user_ctx=nullptr};
     const httpd_uri_t state={.uri="/api/state",.method=HTTP_GET,.handler=stateHandler,.user_ctx=nullptr};
     const httpd_uri_t control={.uri="/api/control",.method=HTTP_POST,.handler=controlHandler,.user_ctx=nullptr};
     if(httpd_register_uri_handler(server,&page)!=ESP_OK || httpd_register_uri_handler(server,&state)!=ESP_OK || httpd_register_uri_handler(server,&control)!=ESP_OK){httpd_stop(server);server=nullptr;}
     else {Serial.print("【WiFi控制中心】浏览器打开 http://");Serial.println(WiFi.localIP());}
    }
   }
  }else if(server){httpd_stop(server);server=nullptr;}
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
void begin(void (*provider)(JsonObject)){
 fillLive=provider;nextId=esp_random();commands=xQueueCreate(1,sizeof(Command));
 if(commands)taskReady=xTaskCreatePinnedToCore(monitor,"PanelWiFi",3072,nullptr,1,nullptr,0)==pdPASS;
 Serial.println(taskReady?"【控制中心】Wi-Fi后台服务已启动，不读取串口。":"【控制中心错误】服务任务启动失败，原操作仍可继续。");
}
} // namespace control_center