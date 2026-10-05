/*
 * 手势语音识别版1.0：个性化六动作 → 拼音选字 → 联想候选 → 语音播报。
 * 打开本文件，保留同目录所有头文件；连接配置在PrivateConfig.h中本地填写。
 * 肌电S：GPIO34；L：GPIO2；灯：5/18/19；OLED与MPU：SDA21/SCL22。
 * MAX98357：BCLK26、LRC25、DIN27；蓝牙模块串口预留RX16/TX17。
 * 校准c/u/d/r/l/g；w/s上下、f分字、b返回、短握确认、长握x联想。
 * 请求/播报期间继续肌电采样，但丢弃操作；结束须回自然位放松再操作。
 * 这里的语音功能是将选中句子读出来，没有麦克风或语音输入。
 * 原main和独立六动作测试工程不改；未上传、未做云端或实物联调。
 */

#include <Arduino.h>
#include "PrivateConfig.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#include <WebServer.h>
#endif
void dispatchAction(char key, bool fromGesture);
bool handleApplicationCommand(char key);
void finishBusyInput();
bool inputHealthy();
#include "GestureSensor.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "driver/i2s_std.h"
#include "InputRouter.h"
#include <cstring>

// ---- 语音设置 ----
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DIN  27
#define VOLUME_PERCENT 40      // 音量 0~100，独立供电后可调回 100
#define PCM_SAMPLE_RATE 16000  // 采样率，必须和下面语音合成请求一致

const char* LLM_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
const char* TTS_URL = "https://dashscope.aliyuncs.com/api/v1/services/audio/tts/SpeechSynthesizer";
const char* DEBUG_SERVER_URL = "http://<your-server>:3000";
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
  Serial.printf("【配置】请连接热点%s，打开192.168.4.1配置。\\n",CONFIG_AP_NAME);
}
void serviceConfigPortal() { if(configPortalActive) configServer.handleClient(); }
#endif

void postDebugLog(const String& message) {
#if defined(ARDUINO_ARCH_ESP32)
  if (savedWifiSsid.length()==0 || WiFi.status()!=WL_CONNECTED) return;
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
#if defined(ARDUINO_ARCH_ESP32)
  if (configPortalActive || savedWifiSsid.length()==0 || WiFi.status()!=WL_CONNECTED ||
      gesture::collecting || gesture::calibration==gesture::Calibration::Complete) return;
  const uint32_t now=millis();
  if (uint32_t(now-lastDebugSync)<1000) return;
  lastDebugSync=now;
  HTTPClient http;
  String base=String(DEBUG_SERVER_URL);
  http.begin(base+"/api/command?device_id="+DEBUG_DEVICE_ID);
  http.setTimeout(3000);
  int code=http.GET();
  if (code==200) {
    String command=http.getString();
    command.trim();
    const char remote=remoteCommandFromJson(command);
    if (remote) {
      gesture::beginCalibration(remote);
      http.end();
      return;
    }
  }
  http.end();
  if (uint32_t(now-lastDebugHeartbeat)<10000) return;
  lastDebugHeartbeat=now;
  http.begin(base+"/api/heartbeat");
  http.addHeader("Content-Type","application/json");
  String body="{\"device_id\":\""+String(DEBUG_DEVICE_ID)+"\",\"step\":\""+String(gesture::calibrationName())+"\",\"progress\":\""+String(static_cast<unsigned>(gesture::captureCount))+"/100\",\"notice\":\""+String(gesture::notice)+"\"}";
  int heartbeatCode=http.POST(body);
  http.end();
#endif
}

// ---- OLED 和字体 ----
U8G2_SSD1306_128X64_NONAME_F_HW_I2C& u8g2 = gesture::display;
#define FONT_CN  u8g2_font_wqy12_t_gb2312   // 中文 12px
#define FONT_BIG u8g2_font_logisoso16_tf    // 大字母 16px

// ---- 状态 ----
enum State { S_PINYIN, S_CAND };
State state = S_PINYIN;

String buf = "";        // 连续输入的拼音（连写，不分字）
int  letterIdx = 0;     // 当前字母 0-25

String cands[8];        // AI 猜出的候选句子（最多 8 句）
int  candCount = 0;     // 候选句个数
int  candIdx = 0;       // 当前选中的候选句

// 忙态只在主循环线程操作；嵌套调用直到最外层结束才开放重新回位。
bool manualReady = false;
unsigned busyDepth = 0;
const char* applicationNotice = nullptr;
uint32_t noticeStartedMs = 0;
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

char curLetter() { return 'a' + letterIdx; }

// ================= 基本输入 ==================

void moveUp() {
  if (state == S_PINYIN) letterIdx = (letterIdx + 25) % 26;
  else if (candCount > 0) candIdx = (candIdx + candCount - 1) % candCount;
}

void moveDown() {
  if (state == S_PINYIN) letterIdx = (letterIdx + 1) % 26;
  else if (candCount > 0) candIdx = (candIdx + 1) % candCount;
}

void appendLetter() {
  buf += curLetter();
}

// 插入一个分隔符 '，表示这个字拼完了
void insertSep() {
  if (state != S_PINYIN) return;
  buf += "'";
}

// 只保留字符串尾部，保证最新输入始终可见
String tailOf(String s, int maxPx) {
  String r = s;
  while (r.length() > 0 && u8g2.getUTF8Width(r.c_str()) > maxPx) {
    r = r.substring(1);
  }
  return r;
}

// ================= 屏幕辅助 ==================

// 中间显示一行中文提示
void drawStatus(String msg) {
  if (!gesture::oledReady) return;
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
  if (!gesture::oledReady) return;
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
        line = ch;
        y += 14;
        if (y > 60) break;
      } else {
        line = test;
      }
    }
    if (line.length() > 0 && y <= 60) u8g2.drawUTF8(0, y, line.c_str());
  } while (u8g2.nextPage());
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
bool getCandidates(String pinyin) {
  if (!networkAvailable()) { unavailableNetwork(); return false; }
  BusyInputScope busy;
  candCount = 0;
  candIdx = 0;
  for (int i = 0; i < 8; i++) cands[i] = "";

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

  String sys = "你是辅助沟通助手。用户是语言障碍患者，只能输入拼音。"
               "用户会在每个字之间用一个单引号 ' 分隔，每个被 ' 隔开的小段就是一个字；"
               "这段可能是完整拼音（如 wo），也可能只是首字母（如 x 代表想）。"
               "输入被单引号分成几段，就对应几个字，你输出的每句中文必须正好是这几个字，"
               "一个字都不能多、也不能少，绝对不要自己加字。"
               "请把用户输入的拼音还原成完整、通顺、符合生活场景的中文句子，"
               "结合喝水、吃饭、上厕所、睡觉、问候、求助等常用场景。"
               "只输出一个 JSON 字符串数组，数组里是候选中文，按可能性从高到低排序。"
               "最多 8 句，但每句必须互不相同；如果没有足够多的不同候选，就少给几句，不要重复凑数。"
               "例如输入 w'x's'j（4 段=4 个字），输出：[\"我想睡觉\",\"我想水饺\",\"我写世界\",\"我闲时间\"]。"
               "绝对不要输出对象、拼音、序号或任何解释，只要纯中文句子的字符串数组。";
  String user = "拼音（单引号 ' 分隔每个字）：" + pinyin;

  String body = "{\"model\":\"qwen-turbo\",\"temperature\":0.2,\"messages\":["
                "{\"role\":\"system\",\"content\":\"" + jsonEscape(sys) + "\"},"
                "{\"role\":\"user\",\"content\":\"" + jsonEscape(user) + "\"}]}";

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("【联想错误】HTTP状态=%d\n", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    Serial.println("【联想错误】响应解析失败");
    return false;
  }
  String content = String(doc["choices"][0]["message"]["content"] | "");
  content.trim();
  Serial.println("【候选】" + content);

  // 容错：AI 有时会在 JSON 外面包一层文字或代码块标记，只取 [ ] 之间的部分
  int lb = content.indexOf('[');
  int rb = content.lastIndexOf(']');
  if (lb != -1 && rb > lb) content = content.substring(lb, rb + 1);

  JsonDocument candDoc;
  if (deserializeJson(candDoc, content)) {
    Serial.println("【候选错误】解析失败：" + content);
    return false;
  }
  JsonArray arr = candDoc.as<JsonArray>();
  candCount = 0;
  for (JsonVariant v : arr) {
    if (candCount >= 8) break;
    String w = "";
    if (v.is<const char*>()) {
      w = v.as<String>();
    } else if (v.is<JsonObject>()) {
      w = v["句"].as<String>();
      if (w == "") w = v["text"].as<String>();
    }
    w.trim();
    if (w.length() > 0) cands[candCount++] = w;
  }
  return candCount > 0;
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

// 把音频下载到内存
String downloadAudio(String url, uint8_t** outBuf, size_t* outLen) {
  if (!outBuf || !outLen) return "音频参数无效";
  *outBuf=nullptr; *outLen=0;
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

  int total = http.getSize();
  if (total <= 0) total = 512 * 1024;
  if (total > 512 * 1024) total = 512 * 1024;

  uint8_t* b = (uint8_t*)malloc(total);
  if (!b) {
    Serial.println("内存不够");
    http.end();
    return "内存不够";
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(30000);
  size_t got = stream->readBytes(b, total);
  http.end();

  *outBuf = b;
  *outLen = got;
  return "";
}

// 用自带的 I2S 驱动把 PCM 数据播出来（16 位单声道）
void playPcm(const uint8_t* pcm, size_t len) {
  if (!pcm || len < 2) return;
  BusyInputScope busy;

  i2s_chan_handle_t tx;
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chan_cfg, &tx, NULL) != ESP_OK) return;

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
    return;
  }
  i2s_channel_enable(tx);

  const uint8_t* data = pcm;
  const uint8_t* end  = pcm + (len - len % 2);
  size_t written;
  static int16_t stereoBuf[4096];

  while (data < end) {
    size_t remain = end - data;
    size_t chunk = remain > 4096 ? 4096 : remain;
    size_t n = chunk / 2;
    const int16_t* s = (const int16_t*)data;
    for (size_t k = 0; k < n; k++) {
      int16_t v = (int16_t)(((int32_t)s[k] * VOLUME_PERCENT) / 100);
      stereoBuf[2 * k]     = v;
      stereoBuf[2 * k + 1] = v;
    }
    i2s_channel_write(tx, stereoBuf, n * 4, &written, portMAX_DELAY);
    data += chunk;
  }

  delay(300);
  i2s_channel_disable(tx);
  i2s_del_channel(tx);

  pinMode(I2S_BCLK, OUTPUT); digitalWrite(I2S_BCLK, LOW);
  pinMode(I2S_LRC, OUTPUT);  digitalWrite(I2S_LRC, LOW);
  pinMode(I2S_DIN, OUTPUT);  digitalWrite(I2S_DIN, LOW);
}

// ================= 核心动作 ==================

// 把一句中文显示出来 + 说出来
void speakSentence(String text) {
  text.trim();
  if (text.length() == 0) return;
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

  uint8_t* pcm = nullptr;
  size_t pcmLen = 0;
  if (downloadAudio(url, &pcm, &pcmLen) == "" && pcm) {
    playPcm(pcm, pcmLen);
    free(pcm);
  }

  delay(2500);
}

// 空格：拼拼音界面=选字母；选句界面=选定这句话并说出来
void confirm() {
  if (state == S_PINYIN) {
    appendLetter();
  } else {  // S_CAND
    if (!networkAvailable()) { unavailableNetwork(); return; }
    BusyInputScope busy;
    if (candIdx >= 0 && candIdx < candCount) {
      speakSentence(cands[candIdx]);
    }
    buf = "";
    letterIdx = 0;
    candIdx = 0;
    state = S_PINYIN;
  }
}

// x：拼音打完了，叫 AI 猜出候选句
void commitWord() {
  if (state != S_PINYIN) return;
  buf.trim();
  if (buf.length() == 0) return;
  if (!networkAvailable()) { unavailableNetwork(); return; }
  BusyInputScope busy;

  drawStatus("联想中...");
  if (getCandidates(buf)) {
    state = S_CAND;
  } else {
    drawStatus("联想失败");
    delay(1500);
  }
}

// b：退格（删拼音字母）；选句界面=返回继续拼
void goBack() {
  if (state == S_CAND) { state = S_PINYIN; return; }
  if (buf.length() > 0) buf.remove(buf.length() - 1);
}

// ================= 绘制 ==================

void drawInputHint() {
  const char* hint=busyDepth ? "正在处理请稍候" : gesture::inputHint(gesture::emgSnapshot());
  if (inputHealthy() && gesture::recognizer.ready()) {
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

// 选句界面：候选句子列表
void drawCand() {
  u8g2.setFont(FONT_CN);
  int start = candIdx - 2;
  if (start < 0) start = 0;
  if (start + 4 > candCount) start = candCount - 4;
  if (start < 0) start = 0;
  for (int i = 0; i < 4 && start + i < candCount; i++) {
    int y = 13 + i * 12;
    String s = (start + i == candIdx ? "> " : "  ") + cands[start + i];
    u8g2.drawUTF8(0, y, tailOf(s, 128).c_str());
  }
  drawInputHint();
}

void draw() {
  if (!gesture::oledReady) return;
  u8g2.setFontPosBaseline();
  u8g2.firstPage();
  do {
    if (state == S_PINYIN) drawPinyin();
    else drawCand();
  } while (u8g2.nextPage());
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
  gesture::recognizer.reset();
  gesture::gravityReady=false;
  gesture::livePoseValid=false;
  gesture::liveValid=false;
  gesture::liveStable=false;
  gesture::livePosture=0;
}
void beginBusyInput() {
  if (busyDepth++==0) finishBusyInput();
}
void endBusyInput() {
  if (busyDepth && --busyDepth==0) finishBusyInput();
}
bool inputHealthy() {
  const gesture::EmgView emg=gesture::emgSnapshot();
  return !busyDepth && gesture::samplingReady && gesture::mpuReady &&
      gesture::liveValid && gesture::liveStable && gesture::livePoseValid &&
      gesture::mpuSampleCurrent() && emg.worn && emg.warmed && !emg.wearUncertain &&
      isfinite(emg.newEnv) && emg.newEnv>=0 && uint32_t(micros()-emg.timeUs)<20000 &&
      emg.wearLosses==gesture::seenWearLosses &&
      emg.wearPauses==gesture::seenWearPauses && emg.faults==gesture::seenFaults;
}
struct AppPort {
  bool calibrated() const {
    return gesture::calibration==gesture::Calibration::Complete && !gesture::collecting;
  }
  bool inputHealthy() const { return ::inputHealthy(); }
  bool serialReady() const { return manualReady; }
  bool needsNetwork(char key) const {
    return (key=='x' && state==S_PINYIN && buf.length()>0) ||
        (key==' ' && state==S_CAND);
  }
  bool networkAvailable() const { return ::networkAvailable(); }
  void unavailableNetwork() { ::unavailableNetwork(); }
  void prepareManualAction() { manualReady=false; gesture::recognizer.reset(); }
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
void dispatchAction(char key, bool fromGesture) {
  if (busyDepth || inputRouter.busy()) return;
  if (inputRouter.dispatch(key,fromGesture)) manualReady=false;
}
bool handleApplicationCommand(char key) {
  if (key>='A' && key<='Z') key=char(key-'A'+'a');
  if (busyDepth || inputRouter.busy()) return true;
  if (key=='w' || key=='s' || key==' ' || key=='x' || key=='f' || key=='b') {
    dispatchAction(key,false);
    return true;
  }
  if (key=='c') manualReady=false;
  return false;
}
void handleInput() { gesture::receiveCommands(); }

void printBrainValues() {
  if (!gesture::brainDataReady) return;
  Serial.print("脑电 信号质量: ");
  Serial.print(gesture::brainSignalQuality);
  Serial.print(" | 专注度: ");
  Serial.print(gesture::brainAttention);
  Serial.print(" | 放松度: ");
  Serial.println(gesture::brainMeditation);
  gesture::brainDataReady=false;
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
  Serial.println("【操作】六步校准后，手势和串口w/s/空格/f/x/b使用同一入口。");
  Serial.println("【提示】联想和播报期间不接收操作；结束后回自然位放松再继续。");
}
void loop() {
  serviceRemoteCalibration();
  checkResetButton();
  serviceConfigPortal();
#if defined(ARDUINO_ARCH_ESP32)
  if (configPortalActive) {
    delay(5);
    return;
  }
#endif
  printBrainValues();
  if (busyDepth || inputRouter.busy()) { discardSerialInput(); delay(1); return; }
  gesture::loop();
  manualReady=appPort.calibrated() && inputHealthy() && gesture::recognizer.ready();
  handleInput();
  const uint32_t now=millis();
  if (uint32_t(now-gesture::lastDrawMs)>=200) {
    gesture::lastDrawMs=now;
    if (!appPort.calibrated() || !gesture::mpuReady || !gesture::samplingReady)
      gesture::drawScreen();
    else draw();
  }
  delay(1);
}
