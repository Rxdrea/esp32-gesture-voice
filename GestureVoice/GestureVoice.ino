/*
 * 手势语音识别版1.2.2：本地模型动作 → 拼音 → 中文联想 → 选句播报。
 * 活动识别入口MlGestureSensor.h；已清理废弃六步规则源码。
 * 上/下翻选择，顺时针分隔，逆时针回退，右侧翻确认，握拳约1秒联想。
 * 五种手腕动作只用MPU，握拳使用肌电及大致自然位，不再六步校准。
 * 手机WiFi/API配置、GPIO33重置、两路预留串口和肌电LED保持。
 * 网络/播放期间继续500Hz肌电采样，动作丢弃，结束后放松再操作。
 */

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
// 模型、中文字形和网络库共用主循环任务，预留足够的嵌套调用余量。
SET_LOOP_TASK_STACK_SIZE(16384);
#endif
#include "PrivateConfig.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#include <WebServer.h>
#endif
void dispatchAction(char key, bool fromGesture);
void dispatchDemoGesture(int label);
bool handleApplicationCommand(char key);
void finishBusyInput();
bool inputHealthy();
bool allowAssociationGesture();
#include "MlGestureSensor.h"
#include <WiFi.h> 
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "driver/i2s_std.h"
#include "InputRouter.h"
#include "PinyinGuard.h"
#include "BrainRemote.h"
#include "ControlCenter.h"
void publishControlCenter(bool force=false);
void applyControlCenterCommands();
void fillControlCenterLive(JsonObject root);
char panelImmediateLines[5][96]={};
bool panelImmediateActive=false;uint8_t panelImmediateLayout=0;
#include <cstring>

// ---- 语音设置 ----
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DIN  27
// 网页音量使用control_center::volume，默认40，范围0～100。
#define PCM_SAMPLE_RATE 16000  // 采样率，必须和下面语音合成请求一致

const char* LLM_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
const char* TTS_URL = "https://dashscope.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer";
const char* DEBUG_SERVER_URL = ""; // 公开版不连接私人调试服务器。
const char* DEBUG_DEVICE_ID = "esp32-main";
uint32_t lastDebugSync=0;
uint32_t lastDebugHeartbeat=0;

#if defined(ARDUINO_ARCH_ESP32)
WebServer configServer(80);
Preferences configPreferences;
String savedWifiSsid, savedWifiPass, savedApiKey;
bool configPortalActive=false;
const char* CONFIG_AP_NAME="ESP32";

void loadSavedConfig() {
  configPreferences.begin("wifi-config",true);
  savedWifiSsid=configPreferences.getString("ssid","");
  savedWifiPass=configPreferences.getString("pass","");
  savedApiKey=configPreferences.getString("api","");
  configPreferences.end();
}
void saveConfig() {
  configPreferences.begin("wifi-config",false);
  configPreferences.putString("ssid",savedWifiSsid);
  configPreferences.putString("pass",savedWifiPass);
  configPreferences.putString("api",savedApiKey);
  configPreferences.end();
}
String configPage() {
  return R"HTML(<!doctype html><html lang='zh-CN'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>手势语音配置</title><style>body{margin:0;background:#eef3f8;color:#17212b;font-family:Arial,'Microsoft YaHei',sans-serif}.box{max-width:460px;margin:32px auto;padding:28px;background:white;border-radius:18px;box-shadow:0 8px 30px #18344b22}h1{margin:0 0 8px;color:#1261a0;font-size:25px}p{color:#607080;line-height:1.6}.label{display:block;margin:17px 0 7px;font-weight:bold}input{box-sizing:border-box;width:100%;padding:13px;border:1px solid #c9d4df;border-radius:10px;font-size:16px}button{width:100%;margin-top:24px;padding:14px;border:0;border-radius:10px;background:#1261a0;color:#fff;font-size:17px;font-weight:bold}small{display:block;margin-top:18px;color:#758493;line-height:1.5}</style><div class='box'><h1>手势语音设备配置</h1><p>连接设备热点后，在这里填写设备需要使用的 Wi‑Fi 和云服务密钥。</p><form method='POST' action='/save'><label class='label'>Wi‑Fi名称</label><input name='ssid' required placeholder='例如：家庭WiFi'><label class='label'>Wi‑Fi密码</label><input name='pass' type='password' placeholder='没有密码可留空'><label class='label'>API密钥</label><input name='api' type='password' required placeholder='请输入云服务API密钥'><button type='submit'>保存并重启设备</button></form><small>保存后设备会自动重启，并尝试连接刚才填写的 Wi‑Fi。</small></div></html>)HTML";
}
void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_NAME);
  configServer.on("/",HTTP_GET,[](){ configServer.send(200,"text/html; charset=utf-8",configPage()); });
  configServer.on("/save",HTTP_POST,[](){ savedWifiSsid=configServer.arg("ssid"); savedWifiPass=configServer.arg("pass"); savedApiKey=configServer.arg("api"); saveConfig(); configServer.send(200,"text/html; charset=utf-8","已保存，设备正在重启"); delay(300); ESP.restart(); });
  configServer.begin(); configPortalActive=true;
  Serial.print("【配置】等待手机配置：连接热点");Serial.print(CONFIG_AP_NAME);Serial.println("，打开192.168.4.1。配置完成前暂停动作输入。");
}
void serviceConfigPortal() { if(configPortalActive) configServer.handleClient(); }
#endif

void postDebugLog(const String& message) {
#if defined(ARDUINO_ARCH_ESP32)
  if (!DEBUG_SERVER_URL[0] || savedWifiSsid.length()==0 || WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String(DEBUG_SERVER_URL)+"/api/log");
  http.addHeader("Content-Type","application/json");
  String body="{\"device_id\":\""+String(DEBUG_DEVICE_ID)+"\",\"message\":\""+message+"\"}";
  http.POST(body);
  http.end();
#endif
}

char remoteCommandFromJson(const String& json) {
  const int key=json.indexOf("\"command\"");
  if (key<0) return 0;
  int colon=json.indexOf(':',key);
  if (colon<0) return 0;
  int quote=json.indexOf('\"',colon+1);
  if (quote<0 || quote+1>=static_cast<int>(json.length())) return 0;
  const char c=json[quote+1];
  return (c=='c'||c=='u'||c=='d'||c=='r'||c=='l'||c=='g') ? c : 0;
}

void serviceRemoteCalibration() {
  // 模型版不再轮询旧六步校准服务，避免同步HTTP打断100Hz姿态采样。
}

// ---- OLED 和字体 ----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C& u8g2 = gesture::display;
#define FONT_CN  u8g2_font_wqy12_t_gb2312   // 中文 12px
#define FONT_BIG u8g2_font_logisoso16_tf    // 大字母 16px

// ---- 状态 ----
enum State { S_PINYIN, S_CAND };
State state = S_PINYIN;

const char* const KEY_GROUPS[]={"'","abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"};
int keyGroup=1; // 1号为分隔，2至9号为字母组；默认ABC
bool selectingLetter=false;
String buf = "";        // 连续输入的拼音（连写，不分字）
bool allowAssociationGesture(){return state==S_PINYIN && !selectingLetter && buf.length()>0;}
int  letterIdx = 0;     // 当前组内字母索引0至3

uint32_t candidateRevision=0;
bool candIsCompletion[8]={false}; // 只供UI显示标签，不加入播报文本
String cands[8];        // AI 猜出的候选句子（最多 8 句）
int  candCount = 0;     // 候选句个数
int  candIdx = 0;       // 当前选中的候选句

// 忙态只在主循环线程操作；嵌套调用直到最外层结束才开放重新回位。
bool manualReady = false;
unsigned busyDepth = 0;
const char* applicationNotice = nullptr;
uint32_t noticeStartedMs = 0;
const char* demoDisplaySentence=nullptr;
uint32_t demoDisplayResultAt=0;
void beginBusyInput();
void endBusyInput();
void drawStatus(String msg);
bool networkAvailable() {
#if defined(ARDUINO_ARCH_ESP32)
  return WiFi.status()==WL_CONNECTED && savedApiKey.length()>0;
#else
  return WiFi.status()==WL_CONNECTED && API_KEY[0]!='\0';
#endif
}
void unavailableNetwork() {
  applicationNotice = WiFi.status()!=WL_CONNECTED ? "网络未连接" : "请填写云服务密钥";
  noticeStartedMs = millis();
  Serial.printf("【提示】%s；可继续本地选字。\n",applicationNotice);
}
class BusyInputScope {
 public:
  BusyInputScope() { beginBusyInput(); }
  ~BusyInputScope() { endBusyInput(); }
  BusyInputScope(const BusyInputScope&) = delete;
  BusyInputScope& operator=(const BusyInputScope&) = delete;
};

char curLetter() { const char* group=KEY_GROUPS[keyGroup];return group[unsigned(letterIdx)%std::strlen(group)]; }

// ================= 基本输入 ==================

void moveUp() {
  if (state == S_PINYIN) {
    if(selectingLetter){int n=std::strlen(KEY_GROUPS[keyGroup]);letterIdx=(letterIdx+n-1)%n;}
    else {keyGroup=(keyGroup+8)%9;letterIdx=0;}
  }
  else if (candCount > 0) candIdx = (candIdx + candCount - 1) % candCount;
}

void moveDown() {
  if (state == S_PINYIN) {
    if(selectingLetter)letterIdx=(letterIdx+1)%std::strlen(KEY_GROUPS[keyGroup]);
    else {keyGroup=(keyGroup+1)%9;letterIdx=0;}
  }
  else if (candCount > 0) candIdx = (candIdx + 1) % candCount;
}

void appendLetter() {
  buf += curLetter();
  selectingLetter=false; // 输入一个字母后回到同一个字母组
}

// 插入一个分隔符 '，表示这个字拼完了
void insertSep() {
  if (state != S_PINYIN) return;
  if(selectingLetter){applicationNotice="先确认当前字母";noticeStartedMs=millis();return;}
  // 九键的分隔键不能制造开头空音节或重复分隔。
  if(!buf.length() || buf[buf.length()-1]=='\'')return;
  buf += "'";
}

// 只保留字符串尾部，保证最新输入始终可见
String tailOf(String s, int maxPx) {
  String r = s;
  while (r.length() > 0 && u8g2.getUTF8Width(r.c_str()) > maxPx) {
    unsigned n=1;while(n<r.length() && (uint8_t(r[n])&0xC0)==0x80)++n;
    r = r.substring(n);
  }
  return r;
}

// ================= 屏幕辅助 ==================

// Busy-only immediate view: recognition is already suspended by BusyInputScope.
// Idle/capture views still use the existing 8-pixel sliced transfer.
void drawDemoMessage(const char* message,const char* footer,const char* badge) {
  memset(panelImmediateLines,0,sizeof(panelImmediateLines));panelImmediateActive=true;panelImmediateLayout=1;
  control_center::textCopy(panelImmediateLines[0],sizeof(panelImmediateLines[0]),"手势语音");
  control_center::textCopy(panelImmediateLines[1],sizeof(panelImmediateLines[1]),message);
  control_center::textCopy(panelImmediateLines[2],sizeof(panelImmediateLines[2]),footer);
  control_center::textCopy(panelImmediateLines[3],sizeof(panelImmediateLines[3]),badge);
  publishControlCenter(true);
  if(!gesture::oledReady || !gesture::oledEnabled)return;
  u8g2.clearBuffer();
  for(unsigned row=0;row<5;++row)demo_ui::drawSlice(u8g2,row,"手势语音",message,footer,badge);
  u8g2.sendBuffer();gesture::invalidateDisplay();
}
// 中间显示一行中文提示
void drawStatus(String msg) {
  if(gesture::demoMode){drawDemoMessage(msg.c_str(),"请稍候再试","提示");return;}
  memset(panelImmediateLines,0,sizeof(panelImmediateLines));panelImmediateActive=true;panelImmediateLayout=0;
  control_center::textCopy(panelImmediateLines[1],sizeof(panelImmediateLines[1]),msg.c_str());publishControlCenter(true);
  if (!gesture::oledReady || !gesture::oledEnabled) return;
  u8g2.setFontPosBaseline();
  u8g2.firstPage();
  do {
    u8g2.setFont(FONT_CN);
    int w = u8g2.getUTF8Width(msg.c_str());
    u8g2.drawUTF8((128 - w) / 2, 32, msg.c_str());
  } while (u8g2.nextPage());
}

// 中文句子自动换行显示
void drawWrapped(String text) {
  if(gesture::demoMode){drawDemoMessage(text.c_str(),"正在准备语音","结果");return;}
  memset(panelImmediateLines,0,sizeof(panelImmediateLines));panelImmediateActive=true;panelImmediateLayout=0;
  if (!gesture::oledReady || !gesture::oledEnabled) {publishControlCenter(true);return;}
  u8g2.setFontPosBaseline();
  u8g2.firstPage();
  do {
    u8g2.setFont(FONT_CN);
    int maxW = 126;
    int y = 14;
    String line = "";
    const char* p = text.c_str();
    while (*p) {
      int len = 1;
      if ((*p & 0x80) == 0) len = 1;
      else if ((*p & 0xE0) == 0xC0) len = 2;
      else if ((*p & 0xF0) == 0xE0) len = 3;
      else if ((*p & 0xF8) == 0xF0) len = 4;
      char bufch[5] = {0};
      memcpy(bufch, p, len);
      String ch = bufch;
      p += len;
      String test = line + ch;
      if (u8g2.getUTF8Width(test.c_str()) > maxW && line.length() > 0) {
        u8g2.drawUTF8(0, y, line.c_str());
        if(y/14-1<5)control_center::textCopy(panelImmediateLines[y/14-1],sizeof(panelImmediateLines[0]),line.c_str());
        line = ch;
        y += 14;
        if (y > 60) break;
      } else {
        line = test;
      }
    }
    if (line.length() > 0 && y <= 60) {u8g2.drawUTF8(0, y, line.c_str());control_center::textCopy(panelImmediateLines[y/14-1],sizeof(panelImmediateLines[0]),line.c_str());}
  } while (u8g2.nextPage());
  publishControlCenter(true);
}

// 把字符串里的双引号、反斜杠转义，避免拼进 JSON 时把格式弄坏
String jsonEscape(String s) {
  String r = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') r += "\\\"";
    else if (c == '\\') r += "\\\\";
    else if (c == '\n') r += "\\n";
    else if (c == '\r') r += "\\r";
    else r += c;
  }
  return r;
}

// ================= 网络：大模型联想 + 语音合成 + 下载 ==================

// 把连续拼音交给 AI，让它分词并猜出 8 句最可能的中文
// 成功返回 true，候选句放进 cands[]、个数放进 candCount
const char* llmErrorHint="联想失败";
int lastLlmHttpStatus=0;
String lastLlmProviderCode;
bool reportLlmFailure(const char* reason,int httpStatus,const String& response) {
  llmErrorHint=reason;lastLlmHttpStatus=httpStatus;lastLlmProviderCode="";
  JsonDocument errorDoc;
  if(response.length() && !deserializeJson(errorDoc,response)) {
    const char* raw=errorDoc["error"]["code"] | "";
    if(!*raw)raw=errorDoc["code"] | "";
    if(!*raw)raw=errorDoc["error"]["type"] | "";
    // 只记录短错误码，不打印原始错误正文、请求头或配置密钥。
    bool safe=std::strlen(raw)<=80 && std::strncmp(raw,"sk-",3)!=0;
    for(const char* p=raw;*p && safe;++p) {
      const char c=*p;
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))safe=false;
    }
    if(safe)lastLlmProviderCode=raw;
    if(lastLlmProviderCode=="Arrearage")llmErrorHint="云服务账户欠费";
    else if(lastLlmProviderCode=="InvalidApiKey" || lastLlmProviderCode=="invalid_api_key")llmErrorHint="密钥或服务区域不匹配";
    else if(lastLlmProviderCode=="AllocationQuota.FreeTierOnly")llmErrorHint="免费额度已用完";
  }
  applicationNotice=llmErrorHint;noticeStartedMs=millis();
  Serial.print("【联想错误】HTTP状态=");Serial.print(httpStatus);
  Serial.print("，原因=");Serial.print(llmErrorHint);
  if(lastLlmProviderCode.length()){Serial.print("，服务错误码=");Serial.print(lastLlmProviderCode);}
  Serial.println();
  return false;
}
// 固定演示词条优先命中；接受带/不带分隔和大小写，不调用大模型。
const char* demoSentence(String input) {
  input.replace("‘","'");input.replace("’","'");input.replace("＇","'");
  if(input.length()>64)return nullptr;
  String key="";
  for(unsigned i=0;i<input.length();++i){char c=input[i];
    if(c=='\'' || c==' ' || c=='\t' || c=='\r' || c=='\n')continue;
    if(c>='A' && c<='Z')c=char(c-'A'+'a');
    if(c<'a' || c>'z')return nullptr;
    key+=c;
  }
  if(key=="wxcf")return "我想吃饭";
  if(key=="zgm")return "在干嘛";
  return nullptr;
}

bool getCandidates(String pinyin) {
  if(const char* fixed=demoSentence(pinyin)) {
    candIdx=0;candCount=1;
    for(int i=0;i<8;++i){cands[i]="";candIsCompletion[i]=false;}
    cands[0]=fixed;++candidateRevision;
    lastLlmHttpStatus=0;lastLlmProviderCode="";llmErrorHint="";
    applicationNotice="本地演示候选";noticeStartedMs=millis();
    Serial.print("【演示候选】");Serial.println(fixed);
    return true;
  }
  if (!networkAvailable()) { unavailableNetwork(); return false; }
  BusyInputScope busy;
  candCount = 0;
  candIdx = 0;
  for (int i = 0; i < 8; i++) {cands[i] = "";candIsCompletion[i]=false;}
  pinyin.replace("‘", "'");pinyin.replace("’", "'");pinyin.replace("＇", "'");pinyin.trim();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, LLM_URL);
  http.setTimeout(30000);
  http.addHeader("Authorization", String("Bearer ") +
#if defined(ARDUINO_ARCH_ESP32)
                savedApiKey
#else
                API_KEY
#endif
  );
  http.addHeader("Content-Type", "application/json");

  String sys = "你是中文拼音输入候选助手，服务于上学和工作中的自由沟通。"
               "输入可以混合完整拼音和简拼，英文单引号分隔音节；单个字母按拼音首字母理解，z/c/s也可对应zh/ch/sh开头。"
               "先列出自然、常用、匹配输入的词句：完整拼音应匹配读音，简拼应匹配首字母；匹配项不添加未输入的语义。"
               "不要局限于生活照护；覆盖问候、学习、工作与日常交流，避免生硬拼字、不通顺词语及不必要的生僻词。"
               "例如z'a可有早安、阻碍、真爱、治安；w'x's'j优先我想睡觉，不要我写世界、我闲时间。"
               "可以额外提供最多两条自然的补全建议，须保留已输入部分的含义，不擅自编造人名、地点或事实。"
               "候选总数最多8条，去重，匹配项排前、补全项排后；数量不足允许少给，绝不凑数。"
               "必须逐字核对拼音；z'g'm可匹配中国梦、这个吗，不匹配工作中或中国故事。补全必须以已给出的某个匹配候选开头。"
               "严禁返回拼音原文、继续输入等占位提示，不生成无关提问。"
               "只返回JSON对象数组，每项必须有text和kind，kind只能是match或completion；text仅放待表达中文，不能带标签或解释。"
               "格式示例：[{\"text\":\"早安\",\"kind\":\"match\"},{\"text\":\"早安，今天有什么安排？\",\"kind\":\"completion\"}]。";
  String user = "请根据以下拼音或简拼给出候选；只返回规定JSON数组：" + pinyin;

  String body = "{\"model\":\"qwen3.7-flash\",\"enable_thinking\":false,\"stream\":false,\"temperature\":0.2,\"messages\":["
                "{\"role\":\"system\",\"content\":\"" + jsonEscape(sys) + "\"},"
                "{\"role\":\"user\",\"content\":\"" + jsonEscape(user) + "\"}]}";

  int code = http.POST(body);
  String resp = code>0 ? http.getString() : String("");
  http.end();
  if (code != 200) {
    const char* reason=code<0?"云服务连接失败":code==401?"密钥或服务区域不匹配":code==403?"云服务权限不足":code==429?"额度或调用次数受限":code==400?"联想请求参数错误":code==404?"模型或接口不可用":"云服务暂时异常";
    if(code==-11)reason="云服务响应超时";
    return reportLlmFailure(reason,code,resp);
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    return reportLlmFailure("服务响应解析失败",code,"");
  }
  if(doc["error"].is<JsonObject>())return reportLlmFailure("云服务返回错误",code,resp);
  String content = String(doc["choices"][0]["message"]["content"] | "");
  content.trim();
  Serial.println("【候选】" + content);

  // 容错：AI 有时会在 JSON 外面包一层文字或代码块标记，只取 [ ] 之间的部分
  int lb = content.indexOf('[');
  int rb = content.lastIndexOf(']');
  if (lb != -1 && rb > lb) content = content.substring(lb, rb + 1);

  JsonDocument candDoc;
  if (deserializeJson(candDoc, content)) {
    return reportLlmFailure("中文候选格式错误",code,"");
  }
  if(!candDoc.is<JsonArray>())return reportLlmFailure("中文候选不是列表",code,"");
  JsonArray arr = candDoc.as<JsonArray>();
  candCount = 0;
  unsigned completions=0;
  // 两轮读取保证匹配项优先；兼容旧字符串数组与旧text/句格式。
  for(unsigned pass=0;pass<2;++pass) for(JsonVariant v:arr) {
    if(candCount>=8)break;
    String w="",kind="match";
    if(v.is<const char*>())w=v.as<String>();
    else if(v.is<JsonObject>()) {
      w=v["text"].as<String>();if(w=="")w=v["句"].as<String>();
      kind=String(v["kind"] | "match");
    } else continue;
    const bool completion=kind=="completion";
    if(kind!="match" && !completion)continue;
    if(completion!=(pass==1) || (completion && completions>=2))continue;
    w.trim();if(w.length()==0 || w.length()>240)continue;
    bool accepted=!completion && pinyin_guard::match(pinyin.c_str(),w.c_str());
    if(completion)for(int j=0;j<candCount;++j)if(!candIsCompletion[j] && pinyin_guard::completion(w.c_str(),cands[j].c_str()))accepted=true;
    if(!accepted){Serial.println("【候选过滤】拼音不匹配或补全无效");continue;}
    bool duplicate=false;for(int j=0;j<candCount;++j)if(cands[j]==w)duplicate=true;
    if(duplicate)continue;
    cands[candCount]=w;candIsCompletion[candCount]=completion;++candCount;
    if(completion)++completions;
  }
  if(candCount==0)return reportLlmFailure("无匹配候选 请补拼音",code,"");
  ++candidateRevision;
  lastLlmHttpStatus=code;lastLlmProviderCode="";llmErrorHint="";
  applicationNotice=nullptr;
  return true;
}

// 语音合成，中文 -> 音频(pcm)网址
String tts(String text) {
  if (!networkAvailable()) { unavailableNetwork(); return ""; }
  BusyInputScope busy;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, TTS_URL);
  http.setTimeout(30000);
  http.addHeader("Authorization", String("Bearer ") +
#if defined(ARDUINO_ARCH_ESP32)
                savedApiKey
#else
                API_KEY
#endif
  );
  http.addHeader("Content-Type", "application/json");

  String body = "{\"model\":\"cosyvoice-v3-flash\",\"input\":{\"text\":\"" + jsonEscape(text) +
                "\",\"voice\":\"longxiaochun_v3\",\"format\":\"pcm\",\"sample_rate\":16000}}";

  int code = http.POST(body);
  if (code != 200) {
    String err = http.getString();
    Serial.printf("【语音错误】HTTP状态=%d\n", code);
    Serial.println(err);
    http.end();
    return "";
  }
  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("【语音错误】响应解析失败");
    Serial.println(resp);
    return "";
  }
  return String(doc["output"]["audio"]["url"] | "");
}

// 1.2.2起网页控制中心常驻占用堆内存，整段音频一次性malloc（最大512KB）在无PSRAM板上会失败，
// 报“内存不够”后不出声。改为边下载边播放：HTTPClient按块写入本类，每块转成双声道后直接送I2S。
class PcmI2sSink : public Stream {
 public:
  explicit PcmI2sSink(i2s_chan_handle_t channel) : tx(channel) {}
  size_t played = 0;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t len) override {
    const size_t used = len;
    if (hasCarry && len) { push((int16_t)(carry | (p[0] << 8))); ++p; --len; hasCarry = false; }
    while (len >= 2) { push((int16_t)(p[0] | (p[1] << 8))); p += 2; len -= 2; }
    if (len) { carry = p[0]; hasCarry = true; }  // 网络块可能在半个采样处切开
    send();
    played += used;
    return used;
  }
 private:
  i2s_chan_handle_t tx;
  uint8_t carry = 0;
  bool hasCarry = false;
  int16_t stereo[1024];
  size_t frames = 0;
  void push(int16_t s) {
    int16_t v = (int16_t)(((int32_t)s * control_center::volume) / 100);
    stereo[2 * frames] = v; stereo[2 * frames + 1] = v;
    if (++frames == 512) send();
  }
  void send() {
    size_t written;
    if (frames) i2s_channel_write(tx, stereo, frames * 4, &written, portMAX_DELAY);
    frames = 0;
  }
};

// 下载PCM（16位单声道）并用自带的I2S驱动同步播放
String streamPlayAudio(String url) {
  if (!networkAvailable()) { unavailableNetwork(); return "网络不可用"; }
  BusyInputScope busy;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;
  HTTPClient http;

  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(plainClient, url);
  }
  http.setTimeout(30000);

  int code = http.GET();
  if (code != 200) {
    Serial.printf("下载错误 %d\n", code);
    http.end();
    return "下载失败";
  }

  i2s_chan_handle_t tx;
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan_cfg, &tx, NULL) != ESP_OK) { http.end(); return "I2S初始化失败"; }

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PCM_SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DIN,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  if (i2s_channel_init_std_mode(tx, &std_cfg) != ESP_OK) {
    i2s_del_channel(tx);
    http.end();
    return "I2S初始化失败";
  }
  i2s_channel_enable(tx);
  control_center::audioActive=true;publishControlCenter(true);

  // writeToStream同时处理固定长度与分块传输，只用约1.4KB临时缓冲
  PcmI2sSink sink(tx);
  int result = http.writeToStream(&sink);
  http.end();

  delay(300);
  i2s_channel_disable(tx);
  i2s_del_channel(tx);
  control_center::audioActive=false;publishControlCenter(true);

  pinMode(I2S_BCLK, OUTPUT); digitalWrite(I2S_BCLK, LOW);
  pinMode(I2S_LRC, OUTPUT);  digitalWrite(I2S_LRC, LOW);
  pinMode(I2S_DIN, OUTPUT);  digitalWrite(I2S_DIN, LOW);

  if (result < 0 && sink.played == 0) {
    Serial.printf("下载错误 %d\n", result);
    return "下载失败";
  }
  return "";
}

// ================= 核心动作 ==================

// 把一句中文显示出来 + 说出来
void speakSentence(String text) {
  text.trim();
  if (text.length() == 0) return;
  control_center::textCopy(control_center::sentence,sizeof(control_center::sentence),text.c_str());
  if(!control_center::speakerEnabled || control_center::volume==0){
    BusyInputScope muted;drawWrapped(text);Serial.println("【播报静音】"+text);return;
  }
  if (!networkAvailable()) { unavailableNetwork(); return; }
  BusyInputScope busy;

  drawWrapped(text);
  Serial.println("【播报】" + text);

  String url = tts(text);
  if (url == "") {
    drawStatus("合成失败");
    delay(2000);
    return;
  }

  String audioError = streamPlayAudio(url);
  if (audioError != "") Serial.println("【语音错误】" + audioError);

  delay(2500);
}

// 空格：拼拼音界面=选字母；选句界面=选定这句话并说出来
void confirm() {
  if (state == S_PINYIN) {
    if(!selectingLetter){
      if(keyGroup==0){insertSep();return;}
      selectingLetter=true;letterIdx=0;return;
    }
    appendLetter();
  } else {  // S_CAND
    if (!networkAvailable()) { unavailableNetwork(); return; }
    BusyInputScope busy;
    if (candIdx >= 0 && candIdx < candCount) {
      speakSentence(cands[candIdx]);
    }
    buf = "";
    letterIdx = 0;keyGroup=1;selectingLetter=false;
    candIdx = 0;
    state = S_PINYIN;
  }
}

// x：拼音打完了，叫 AI 猜出候选句
void commitWord() {
  if (state != S_PINYIN) return;
  if(selectingLetter){applicationNotice="先确认当前字母";noticeStartedMs=millis();return;}
  buf.trim();
  if (buf.length() == 0) return;
  if (!networkAvailable() && !demoSentence(buf)) { unavailableNetwork(); return; }
  BusyInputScope busy;

  drawStatus(demoSentence(buf)?"演示候选":"联想中...");
  if (getCandidates(buf)) {
    state = S_CAND;
  } else {
    drawStatus(llmErrorHint);
    delay(1500);
  }
}

// b：退格（删拼音字母）；选句界面=返回继续拼
void goBack() {
  if (state == S_CAND) { state = S_PINYIN;selectingLetter=false; return; }
  if(selectingLetter){selectingLetter=false;letterIdx=0;return;}
  if (buf.length() > 0) buf.remove(buf.length() - 1);
}

// ================= 绘制 ==================

void drawInputHint() {
  const char* hint=busyDepth ? "正在处理请稍候" : gesture::inputHint(gesture::emgSnapshot());
  if (inputHealthy() && gesture::ready()) {
    if (applicationNotice && uint32_t(millis()-noticeStartedMs)<2000) hint=applicationNotice;
    else if (manualReady) hint=WiFi.status()==WL_CONNECTED ? "可以操作" : "可操作 网络未连接";
  }
  u8g2.setFont(FONT_CN);
  u8g2.drawUTF8(0,63,hint);
}

// 拼拼音界面：当前拼音 + 当前字母
void drawPinyin() {
  u8g2.setFont(FONT_CN);
  if (buf.length() == 0) u8g2.drawUTF8(0, 14, "（开始拼拼音）");
  else u8g2.drawUTF8(0, 14, tailOf(buf, 128).c_str());

  u8g2.setFont(FONT_BIG);
  char one[2] = { curLetter(), '\0' };
  int w = u8g2.getUTF8Width(one);
  u8g2.drawUTF8((128 - w) / 2, 36, one);

  u8g2.setFont(FONT_CN);
  drawInputHint();
}

String candidatePreview(const String& text,int width,bool selected) {
  u8g2.setFont(FONT_CN);
  static int last=-1;static uint32_t since=0,revision=0;
  if(last!=candIdx || revision!=candidateRevision){last=candIdx;revision=candidateRevision;since=millis();}
  unsigned page=selected ? uint32_t(millis()-since)/1600:0;
  String pieces[16];unsigned count=0;String part="";
  const char* p=text.c_str();
  while(*p && count<16){const char* begin=p;uint32_t cp=0;if(!pinyin_guard::next(p,cp))break;
    String ch="";while(begin<p)ch+=*begin++;
    if(part.length() && u8g2.getUTF8Width((part+ch).c_str())>width){pieces[count++]=part;part="";if(count>=16)break;}
    part+=ch;
  }
  if(part.length()&&count<16)pieces[count++]=part;
  return count?pieces[page%count]:String("");
}

// 选句界面：候选句子列表
void drawCand() {
  u8g2.setFont(FONT_CN);
  int start = candIdx - 2;
  if (start < 0) start = 0;
  if (start + 4 > candCount) start = candCount - 4;
  if (start < 0) start = 0;
  for (int i = 0; i < 4 && start + i < candCount; i++) {
    int y = 13 + i * 12;
    String s = (start + i == candIdx ? "> " : "  ") + String(candIsCompletion[start+i]?"[补] ":"") + candidatePreview(cands[start+i],candIsCompletion[start+i]?90:112,start+i==candIdx);
    u8g2.drawUTF8(0, y, s.c_str());
  }
  drawInputHint();
}

void draw() {
  panelImmediateActive=false;
  if (!gesture::oledReady || !gesture::oledEnabled) return;
  if(gesture::demoMode){
    const bool notice=applicationNotice && uint32_t(millis()-noticeStartedMs)<2000;
    const auto phase=gesture::demoRecognizer.state;
    if(!gesture::mpuSampleCurrent())gesture::queueDemoScreen("连接传感器","请稍候","连接");
    else if(phase==DemoRecognizer::State::Capture)gesture::queueDemoScreen("正在识别","画完停住片刻","识别");
    else if(demoDisplaySentence && uint32_t(millis()-demoDisplayResultAt)<3000)
      gesture::queueDemoScreen(demoDisplaySentence,notice?applicationNotice:"放松后继续表达","结果");
    else if(notice)gesture::queueDemoScreen("温馨提示",applicationNotice,"提示");
    else if(phase==DemoRecognizer::State::Ready){
      const String address=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("等待WiFi连接");
      gesture::queueDemoScreen("准备就绪",address.c_str(),"就绪");
    }
    else if(phase==DemoRecognizer::State::Recovery)gesture::queueDemoScreen("请再试一次",gesture::demoRecognizer.hint,"提示");
    else gesture::queueDemoScreen("请先放松","保持自然姿态","等待");
    return;
  }
  String hint=busyDepth ? "正在处理请稍候" : gesture::inputHint(gesture::emgSnapshot());
  if(applicationNotice && uint32_t(millis()-noticeStartedMs)<2000)hint=applicationNotice;
  if(state==S_PINYIN) {
    String input=buf.length()?tailOf(buf,128):String("（开始拼拼音）");
    if(!selectingLetter){
      String status="选组 右侧翻进入";
      if(applicationNotice && uint32_t(millis()-noticeStartedMs)<2000)status=applicationNotice;
      gesture::queueScreen(input.c_str(),"1 ' |2 ABC|3 DEF","4 GHI|5 JKL|6 MNO","7PQRS|8 TUV|9WXYZ",status.c_str(),-1,keyGroup);
    } else {
      char letter[2]={curLetter(),0};
      String group="字母组 "+String(keyGroup+1)+" "+String(KEY_GROUPS[keyGroup]);
      gesture::queueScreen(input.c_str(),letter,group.c_str(),"右侧确认 逆圈返回",nullptr,1);
    }
  } else {
    int start=candIdx-2;if(start<0)start=0;if(start+4>candCount)start=candCount-4;if(start<0)start=0;
    String rows[4];for(int i=0;i<4 && start+i<candCount;++i)rows[i]=(start+i==candIdx?"> ":"  ")+String(candIsCompletion[start+i]?"[补] ":"")+candidatePreview(cands[start+i],candIsCompletion[start+i]?90:112,start+i==candIdx);
    gesture::queueScreen(rows[0].c_str(),rows[1].c_str(),rows[2].c_str(),rows[3].c_str(),hint.c_str());
  }
}

// ================= 输入 ==================
void discardSerialInput() {
  // 只清本次已有字节，避免外部持续发送使主循环卡住。
  int available=Serial.available();
  while (available-- > 0) Serial.read();
}
void finishBusyInput() {
  discardSerialInput();
  manualReady=false;
  gesture::resetInput();
}
void beginBusyInput() {
  if (busyDepth++==0) finishBusyInput();
  publishControlCenter(true);
}
void endBusyInput() {
  if (busyDepth && --busyDepth==0) finishBusyInput();
  publishControlCenter(true);
}
bool inputHealthy() {
  // 手腕动作只依赖新鲜MPU数据；握拳佩戴要求在按键x入口单独检查。
  return !busyDepth && gesture::inputHealthy();
}
struct AppPort {
  bool calibrated() const {
    return true; // 模型固化在程序中，无需旧六步校准。
  }
  bool inputHealthy() const { return ::inputHealthy(); }
  bool serialReady() const { return manualReady; }
  bool needsNetwork(char key) const {
    return (key=='x' && state==S_PINYIN && buf.length()>0 && !demoSentence(buf)) ||
        (key==' ' && state==S_CAND);
  }
  bool networkAvailable() const { return ::networkAvailable(); }
  void unavailableNetwork() { ::unavailableNetwork(); }
  void prepareManualAction() { manualReady=false; gesture::resetInput(); }
  void beginBusy() { beginBusyInput(); }
  void endBusy() { endBusyInput(); }
  void up() { moveUp(); }
  void down() { moveDown(); }
  void confirm() { ::confirm(); }
  void requestCandidates() { commitWord(); }
  void separator() { insertSep(); }
  void back() { goBack(); }
};
AppPort appPort;
merged::InputRouter<AppPort> inputRouter(appPort);
void dispatchDemoGesture(int label) {
  if(label<0 || label>1 || !gesture::demoMode || gesture::demoAuthorized!=label ||
     busyDepth || inputRouter.busy() || !gesture::mpuReady || configPortalActive)return;
  // label按值保存；忙态重置会清除授权与整段窗口，禁止播放前后数据拼接。
  const char* sentence=label==0?"我想吃饭":"你们好啊";
  demoDisplaySentence=sentence;demoDisplayResultAt=millis();
  BusyInputScope busy;
  Serial.print("【演示句子】");Serial.println(sentence);
  speakSentence(String(sentence)); // 原云端TTS + PCM播放，保留网络检查。
  demoDisplayResultAt=millis(); // UI result hold only; does not block input.
}
void dispatchAction(char key, bool fromGesture) {
  if (busyDepth || inputRouter.busy() || !gesture::allowAction(key)) return;
  if(fromGesture && gesture::authorizedKey!=key)return;
  if (inputRouter.dispatch(key,fromGesture)) {
    manualReady=false;
    Serial.print("【输入结果】拼音=");Serial.print(buf);
    Serial.print("，当前键组=");Serial.print(keyGroup+1);
    Serial.print("，输入层=");Serial.print(selectingLetter?"选字母":"选组");
    Serial.print("，当前字母=");Serial.print(curLetter());
    Serial.print("，界面=");Serial.println(state==S_PINYIN?"拼音":"中文候选");
  }
}
bool handleApplicationCommand(char key) {
  if (key>='A' && key<='Z') key=char(key-'A'+'a');
  if (key=='e') { brain_remote::printStatus(); return true; }
  if (busyDepth || inputRouter.busy()) return true;
  if (key=='m') { if(!configPortalActive){manualReady=false;gesture::setDemoMode(!gesture::demoMode);} return true; }
  if (key=='w' || key=='s' || key==' ' || key=='x' || key=='f' || key=='b') {
    dispatchAction(key,false);
    return true;
  }
  if (key=='c') manualReady=false;
  return false;
}
void handleInput() { gesture::receiveCommands(); }


// 只由主循环发布固定快照；HTTP线程不读取动态String或MPU总线。
void publishControlCenter(bool force) {
 static uint32_t last=0;const uint32_t now=millis();
 if(!force && uint32_t(now-last)<200)return;last=now;
 control_center::Runtime view;
 view.capturedMs=now;view.imuMs=gesture::lastMpuMs;
 view.haveImu=gesture::haveMpu;view.mpuReady=gesture::mpuReady;
 view.demo=gesture::demoMode;view.busy=busyDepth>0;
 view.oledReady=gesture::oledReady;view.oledEnabled=gesture::oledEnabled;
 view.speakerEnabled=control_center::speakerEnabled;view.audioActive=control_center::audioActive;view.volume=control_center::volume;
 view.layout=panelImmediateActive?panelImmediateLayout:gesture::screenLayout;
 view.modelValid=gesture::demoMode && gesture::panelModelValid;view.modelAtMs=gesture::panelModelAt;view.modelLabel=gesture::panelModelLabel;
 for(unsigned i=0;i<6;++i)view.imu[i]=gesture::panelImu[i];
 view.controlGapMs=gesture::longestControlGap;view.captureDrops=gesture::captureGapDrops;
 view.modelMs=gesture::panelModelMs;
 view.matchDistance=gesture::panelDistance;view.alternativeDistance=gesture::panelAlternative;
 view.durationMs=gesture::panelDuration;
 control_center::textCopy(view.hint,sizeof(view.hint),view.busy?"正在处理语音或联想":gesture::inputHint(gesture::emgSnapshot()));
 control_center::textCopy(view.action,sizeof(view.action),gesture::lastAction);
 control_center::textCopy(view.sentence,sizeof(view.sentence),control_center::sentence);
 control_center::textCopy(view.pinyin,sizeof(view.pinyin),buf.c_str());
 if(panelImmediateActive)memcpy(view.lines,panelImmediateLines,sizeof(view.lines));
 else memcpy(view.lines,gesture::screen,sizeof(view.lines));
 control_center::publish(view);
}
// 运行在HTTP后台任务，只调用已有带锁快照，不碰主循环状态。
void fillControlCenterLive(JsonObject root) {
 const auto emg=gesture::emgSnapshot();const auto eeg=brain_remote::snapshot();
 const auto pose=gesture::panelImuSnapshot();
 const uint32_t now=millis();const uint32_t emgAge=uint32_t(micros()-emg.timeUs)/1000;
 JsonObject imu=root["imu"].to<JsonObject>();
 const uint32_t poseAge=pose.sequence?uint32_t(now-pose.timeMs):UINT32_MAX;
 const bool poseValid=pose.valid && pose.sequence && poseAge<=400;
 imu["valid"]=poseValid;imu["age_ms"]=poseAge;imu["sequence"]=pose.sequence;
 imu["reason"]=poseValid?"ok":root["busy"].as<bool>()?"busy":!pose.sequence?"waiting":!pose.valid?"read_error":"stale";
 JsonArray acc=imu["accel"].to<JsonArray>(),gyro=imu["gyro"].to<JsonArray>();
 for(unsigned i=0;i<3;++i){acc.add(pose.axes[i]);gyro.add(pose.axes[i+3]);}
 imu["pitch"]=atan2f(pose.axes[0],sqrtf(pose.axes[1]*pose.axes[1]+pose.axes[2]*pose.axes[2]))*57.29578f;
 imu["roll"]=atan2f(pose.axes[1],pose.axes[2])*57.29578f;
 JsonObject muscle=root["emg"].to<JsonObject>();
 muscle["value"]=std::isfinite(emg.newEnv)?emg.newEnv:0;
 muscle["sequence"]=emg.sequence;muscle["age_ms"]=emgAge;
 muscle["valid"]=emg.sequence && emg.warmed && std::isfinite(emg.newEnv) && emgAge<=100;
 muscle["worn"]=emg.worn;muscle["warmed"]=emg.warmed;muscle["uncertain"]=emg.uncertain;
 const uint8_t flags=brain_remote::flags(eeg,now);
 const bool valid=(flags&(EEG_FRESH|EEG_CONTACT|EEG_DECISION))==(EEG_FRESH|EEG_CONTACT|EEG_DECISION);
 JsonObject brain=root["eeg"].to<JsonObject>();
 brain["attention"]=eeg.attention;brain["meditation"]=eeg.meditation;brain["quality"]=eeg.quality;
 brain["valid"]=valid;
 brain["meditation_valid"]=eeg.haveMeditation && (flags&EEG_CONTACT) && uint32_t(now-eeg.meditationMs)<=3000;
 brain["meditation_age_ms"]=eeg.haveMeditation?uint32_t(now-eeg.meditationMs):UINT32_MAX;
 brain["age_ms"]=eeg.haveAttention?uint32_t(now-eeg.attentionMs):UINT32_MAX;
 brain["command"]=eeg.command==1?"on":eeg.command==2?"off":"wait";
 brain["pending"]=eeg.pending==1?"on":eeg.pending==2?"off":"wait";brain["confirm"]=eeg.confirmCount;
 JsonObject radio=root["radio"].to<JsonObject>();
 radio["ready"]=eeg.wirelessReady;radio["channel"]=eeg.channel;radio["submitted"]=eeg.submitted;
 radio["errors"]=eeg.sendErrors;radio["parse_errors"]=eeg.parseErrors;radio["receiver_confirmed"]=false;
 JsonObject output=root["output"].to<JsonObject>();output["command"]=brain["command"];
 JsonObject expected=output["expected"].to<JsonObject>();
 expected["25"]=eeg.command==2?1:0;expected["2"]=eeg.command==1?1:0;expected["26"]=eeg.command==1?1:0;output["verified"]=false;
}
void applyControlCenterCommands(){
 control_center::Command cmd;
 if(!control_center::take(cmd))return;
 if(busyDepth || inputRouter.busy() || configPortalActive || uint32_t(millis()-cmd.createdMs)>2000){
  control_center::complete(cmd.id,false,"设备正忙或指令已过期，请重试");return;
 }
 switch(cmd.action){
  case control_center::Action::Oled:
   if(!gesture::oledReady){control_center::complete(cmd.id,false,"OLED未就绪");return;}
   gesture::oledEnabled=cmd.value;gesture::display.setPowerSave(cmd.value?0:1);
   if(cmd.value)gesture::invalidateDisplay();break;
  case control_center::Action::Speaker:control_center::speakerEnabled=cmd.value;break;
  case control_center::Action::Volume:control_center::volume=uint8_t(cmd.value);break;
  case control_center::Action::Mode:
   if(gesture::demoMode!=bool(cmd.value)){manualReady=false;gesture::setDemoMode(cmd.value);}
   break;
 }
 control_center::complete(cmd.id,true,"设备已执行");publishControlCenter(true);
}

void printBrainValues() {
  brain_remote::printStatus();
}
void sendSerial1Test() {
  static uint32_t lastSent=0;
  const uint32_t now=millis();
  if (uint32_t(now-lastSent)>=1000) {
    lastSent=now;
    gesture::extraSerial.write('s');
  }
}
void clearSavedNetworkConfigAndRestart() {
#if defined(ARDUINO_ARCH_ESP32)
  Preferences preferences;
  preferences.begin("wifi-config", false);
  preferences.clear();
  preferences.end();
  gesture::clearSavedCalibration();
  WiFi.softAPdisconnect(true);
#endif
  Serial.println("【重置】已清除保存的Wi-Fi和API配置，正在重启。");
  delay(300);
#if defined(ARDUINO_ARCH_ESP32)
  ESP.restart();
#endif
}

void checkResetButton() {
  static uint32_t pressedAt=0;
  static bool handled=false;
  const bool pressed=digitalRead(gesture::RESET_BUTTON_PIN)==LOW;
  if (!pressed) {
    pressedAt=0;
    handled=false;
    return;
  }
  if (pressedAt==0) pressedAt=millis();
  if (!handled && uint32_t(millis()-pressedAt)>=gesture::RESET_HOLD_MS) {
    handled=true;
    clearSavedNetworkConfigAndRestart();
  }
}

// ================= 主流程 ==================
void setup() {
  gesture::setup();
  pinMode(gesture::RESET_BUTTON_PIN,INPUT_PULLUP);
  Serial.print("【启动】GPIO33重置按键电平=");Serial.println(digitalRead(gesture::RESET_BUTTON_PIN));
  manualReady=false;
#if defined(ARDUINO_ARCH_ESP32)
  loadSavedConfig();
  if (savedWifiSsid.length()==0) {
    startConfigPortal();
    gesture::drawConfigWaitScreen();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedWifiSsid.c_str(),savedWifiPass.c_str());
    Serial.println("【网络】正在连接已保存的Wi-Fi。");
  }
#else
  Serial.println("【网络】当前编译环境不支持ESP32配置页面。");
#endif
  Serial.println("【操作】默认两动作演示：三角形说我想吃饭，Z说你们好啊；串口m切回原拼音操作。");
  Serial.println("【提示】联想和播报期间不接收操作；结束后回自然位放松再继续。");
  brain_remote::begin(gesture::bluetoothSerial);
  publishControlCenter(true);
  control_center::begin(fillControlCenterLive);
  Serial.println("【脑电调试】输入e查看状态；判断结果发送到第二块，不改变拼音操作。");
}
void loop() {
  serviceRemoteCalibration();
  checkResetButton();
  serviceConfigPortal();
  applyControlCenterCommands();
  publishControlCenter();
#if defined(ARDUINO_ARCH_ESP32)
  if (configPortalActive) {
    // 配置页期间仍可用p查询采样/佩戴，不能把等待配置误认为程序卡死。
    gesture::receiveCommands();
    gesture::serviceDisplay();
    delay(1);
    return;
  }
#endif
  // 脑电数据在后台独立处理，输入e查看；不持续打印干扰手势。
  if (busyDepth || inputRouter.busy()) { discardSerialInput(); delay(1); return; }
  gesture::repeatIntervalMs=state==S_CAND?450:300;
  gesture::loop();
  manualReady=appPort.calibrated() && inputHealthy() && gesture::ready();
  handleInput();
  const uint32_t now=millis();
  const uint32_t drawInterval=gesture::phase==gesture::Phase::Scroll?60:200;
  if (uint32_t(now-gesture::lastDrawMs)>=drawInterval) {
    gesture::lastDrawMs=now;
    if (!gesture::mpuReady)
      gesture::drawScreen();
    else draw();
  }
  gesture::serviceDisplay();
  delay(1);
}
