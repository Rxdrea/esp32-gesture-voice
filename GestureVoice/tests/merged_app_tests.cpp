// Includes the real merged sketch, recognizer, calibration, routing and business logic.
// Only Arduino/hardware/network boundaries are replaced. ArduinoJson is the installed library.
// Every named case runs in its own process. --list enumerates the runnable cases.
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
#include <cstdlib>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include "../GestureVoice.ino"

namespace test {
using sixops::Vec3;
void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
void fixture() {
  Wire.reset(); host::setMillis(1000); Serial.input.clear(); Serial.clearOutput();
  host::wifiStatus=WL_CONNECTED;
  gesture::mpuReady=true; gesture::mpuAddress=0x68; gesture::samplingReady=true;
  gesture::sharedEmg.worn=true; gesture::sharedEmg.warmed=true;
  gesture::sharedEmg.raw=512; gesture::sharedEmg.newEnv=2.0f;
  gesture::sharedEmg.oldEnv=2; gesture::sharedEmg.sequence=1; gesture::sharedEmg.timeUs=micros();
}
Vec3 direction(char key) {
  switch(key) {
    case 'w': return {0.95212939f,0,-0.30569530f};
    case 's': return {-0.61566148f,0,0.78801075f};
    case 'f': return {0,0.81915204f,0.57357644f};
    case 'b': return {0,-0.95105652f,0.30901699f};
    default: return {0,0,1};
  }
}
void sampleRegisters(Vec3 acceleration,int gyro=0) {
  std::vector<uint8_t> bytes(14,0);
  auto word=[&](size_t offset,int value) { uint16_t x=static_cast<uint16_t>(value); bytes[offset]=x>>8; bytes[offset+1]=x&255; };
  word(0,static_cast<int>(lroundf(acceleration.x*16384)));
  word(2,static_cast<int>(lroundf(acceleration.y*16384)));
  word(4,static_cast<int>(lroundf(acceleration.z*16384))); word(8,gyro);
  Wire.registers[0x3A]=1;
  for(size_t i=0;i<bytes.size();++i) Wire.registers[static_cast<uint8_t>(0x3B+i)]=bytes[i];
}
void frame(char pose='n',float envelope=2.0f,bool mainLoop=true) {
  host::advanceMillis(20); gesture::sharedEmg.newEnv=envelope;
  ++gesture::sharedEmg.sequence; gesture::sharedEmg.timeUs=micros();
  sampleRegisters(direction(pose));
  if(mainLoop) ::loop(); else gesture::controlStep();
}
void hold(char pose,unsigned frames,float envelope=2.0f,bool mainLoop=true) {
  for(unsigned i=0;i<frames;++i) frame(pose,envelope,mainLoop);
}
void capture(char command,char pose,float envelope=2.0f) {
  gesture::sharedEmg.newEnv=envelope;
  Serial.feed(std::string(1,command)); gesture::receiveCommands();
  require(gesture::collecting && gesture::captureKind==command,"calibration command must begin requested real collection");
  const uint32_t start=gesture::captureStartedMs;
  for(unsigned i=0;i<100;++i) {
    host::setMillis(start+3000+i*20);
    gesture::calibrationStep(millis(),gesture::sharedEmg,true,direction(pose),{0,0,0});
  }
  require(!gesture::collecting && gesture::captureCount==100,"calibration step must accept 100 real samples");
}
void calibrate() {
  capture('c','n'); capture('u','w'); capture('d','s'); capture('r','f'); capture('l','b'); capture('g','n',20);
  require(gesture::calibration==gesture::Calibration::Complete,"six actual calibration steps must finish");
  require(gesture::eventCount==0 && letterIdx==0 && buf=="","calibration must not alter application input");
  gesture::sharedEmg.newEnv=2;
}
void rearm() {
  hold('n',70);
  require(std::string(gesture::recognizer.stateName())=="可以操作","neutral relaxed samples must rearm real recognizer");
  require(manualReady,"main loop must permit manual input after healthy neutral rearm");
}
void ready() { fixture(); calibrate(); rearm(); }
void key(char c) { Serial.feed(std::string(1,c)); frame(); }
std::string candidatesJson() { return R"({"choices":[{"message":{"content":"[\"你好\",\"您好\"]"}}]})"; }
void assertBusyExit() {
  require(!manualReady,"busy completion must not immediately enable serial input");
  require(!gesture::gravityReady && !gesture::livePoseValid,"busy completion must discard cached gravity and pose");
  require(Serial.available()==0,"bytes arriving during busy work must be discarded");
  require(std::string(gesture::recognizer.stateName())=="回位并放松","busy completion must leave recognizer locked");
}
void injectBusy() {
  require(!manualReady,"input must already be blocked when the external operation starts");
  const int before=letterIdx;
  dispatchAction('s',true); dispatchAction('w',false);
  require(letterIdx==before,"both manual and gesture dispatch must be blocked during busy work");
  Serial.feed("wS fbx");
}
// Catches an unguarded original-main serial switch, not merely a missing interface.
void uncalibrated_serial_is_blocked() {
  fixture(); Serial.feed("wS fbx"); frame();
  require(letterIdx==0 && buf=="" && state==S_PINYIN,"uncalibrated serial must not change letter or text");
  require(host::httpRequests.empty(),"uncalibrated serial must not make network requests");
}
// Catches emitAction remaining diagnostic-only or a mistaken Ready recheck after it locks.
void calibrated_up_changes_letter() {
  fixture(); calibrate(); hold('n',70,2,false);
  require(std::string(gesture::recognizer.stateName())=="可以操作","controlStep must truly rearm first");
  hold('w',40,2,false);
  require(gesture::eventCount==1 && gesture::lastKey=='w',"real controlStep must recognize exactly one up event");
  require(letterIdx==25,"recognized up must change application letterIdx from 0 to 25");
  hold('w',80,2,false); require(letterIdx==25 && gesture::eventCount==1,"held up must not repeat");
}
void real_six_actions_reach_business() {
  ready(); hold('w',40); require(letterIdx==25,"up maps to previous letter");
  rearm(); hold('s',40); require(letterIdx==0,"down maps to next letter");
  rearm(); hold('n',25,20); hold('n',14); require(buf=="a","short grip must append current letter");
  rearm(); hold('f',40); require(buf=="a'","separator pose must append apostrophe");
  rearm(); hold('b',40); require(buf=="a","back pose must remove last character");
  rearm(); host::reply(200,candidatesJson()); hold('n',80,20);
  require(state==S_CAND && candCount==2 && cands[0]=="你好","long grip must run real candidate parsing and selection");
  require(host::httpRequests.size()==1,"long grip must make exactly one candidate request");
}
void serial_six_keys_reach_business() {
  ready(); key('W'); require(letterIdx==25,"W maps to previous letter");
  rearm(); key('S'); require(letterIdx==0,"S maps to next letter");
  rearm(); key(' '); require(buf=="a","space chooses a letter");
  rearm(); key('F'); require(buf=="a'","F inserts separator");
  rearm(); key('B'); require(buf=="a","B removes separator");
  rearm(); host::reply(200,candidatesJson()); key('X');
  require(state==S_CAND && candCount==2 && cands[1]=="您好","X invokes real LLM parsing");
}
void same_frame_bad_emg_blocks_serial() {
  ready(); ++gesture::sharedEmg.faults; Serial.feed("w "); frame();
  require(letterIdx==0 && buf=="","current bad EMG must beat previous frame Ready before serial processing");
}
void same_frame_mpu_failure_blocks_serial() {
  ready(); gesture::lastCheckMs=millis(); Wire.queueFailure(0x3A); Serial.feed("w "); frame();
  require(letterIdx==0 && buf=="","failed current MPU read must prevent serial action");
}
void changed_snapshot_blocks_serial() {
  ready(); ++gesture::sharedEmg.faults; Serial.feed("w"); gesture::receiveCommands();
  require(letterIdx==0,"fault appearing after controlStep must invalidate cached manualReady");
}
void stale_snapshot_blocks_serial() {
  ready(); host::advanceMillis(20); Serial.feed("w"); gesture::receiveCommands();
  require(letterIdx==0,"20ms-old EMG must not reuse last frame manualReady");
}
void lost_wear_blocks_serial() {
  ready(); gesture::sharedEmg.worn=false; ++gesture::sharedEmg.wearLosses; Serial.feed("w "); frame();
  require(letterIdx==0 && buf=="","wear loss must block local input immediately");
}
void unstable_pose_blocks_serial() {
  ready(); host::advanceMillis(20); ++gesture::sharedEmg.sequence; gesture::sharedEmg.timeUs=micros();
  sampleRegisters(direction('n'),3930); Serial.feed("w"); ::loop();
  require(letterIdx==0,"moving wrist must not reuse last stable frame for serial input");
}
void llm_success_discards_busy_input() {
  ready(); host::onHttp=injectBusy; host::reply(200,candidatesJson());
  require(getCandidates("ni'hao"),"real candidate request must parse installed ArduinoJson response");
  require(candCount==2 && cands[0]=="你好","actual Chinese candidates must survive parsing");
  assertBusyExit(); host::onHttp={};
  hold('w',100); require(letterIdx==0,"held up after busy must not replay an old gesture or serial byte");
  hold('n',100,20); require(buf=="" && host::httpRequests.size()==1,"held grip after busy must not confirm or request again");
  rearm(); key('s'); require(letterIdx==1,"neutral relaxed recovery must restore real serial dispatch");
}
void llm_http_failure_restores_gate() {
  ready(); host::onHttp=injectBusy; host::reply(500,"local simulated failure");
  require(!getCandidates("ni"),"HTTP error must fail candidate lookup"); assertBusyExit(); host::onHttp={};
  rearm(); key(' '); require(buf=="a","local input must recover after failed candidate request");
}
void llm_invalid_json_restores_gate() {
  ready(); host::onHttp=injectBusy; host::reply(200,"not-json");
  require(!getCandidates("ni"),"malformed server response must fail candidate parsing"); assertBusyExit(); host::onHttp={};
  rearm(); key('s'); require(letterIdx==1,"local input must recover after JSON parsing failure");
}
void tts_failure_restores_gate() {
  ready(); host::onHttp=injectBusy; host::reply(503,"local simulated failure");
  require(tts("你好")=="","TTS HTTP failure must return no audio URL"); assertBusyExit(); host::onHttp={};
  rearm(); key('s'); require(letterIdx==1,"local input must recover after TTS failure");
}
void tts_success_uses_real_json() {
  ready(); host::onHttp=injectBusy; host::reply(200,R"({"output":{"audio":{"url":"https://audio.invalid/sample.pcm"}}})");
  require(tts("你好")=="https://audio.invalid/sample.pcm","real TTS parser must extract nested URL"); assertBusyExit();
}
void wifi_offline_local_only() {
  ready(); host::wifiStatus=WL_DISCONNECTED; key('s'); require(letterIdx==1,"offline WiFi must not block local selection");
  rearm(); key(' '); require(buf=="b","offline WiFi must not block choosing letters");
  rearm(); key('x'); require(state==S_PINYIN && buf=="b","offline candidate request must preserve entered text");
  require(!getCandidates("ni") && tts("你好")=="","offline network helpers must report unavailable");
  uint8_t* pcm=nullptr; size_t len=0; require(downloadAudio("https://audio.invalid/x",&pcm,&len)!="","offline audio download must fail");
  require(host::httpRequests.empty() && host::httpBegins==0,"offline operations must never begin HTTP");
}
void pcm_playback_discards_busy_input() {
  ready(); host::onI2sWrite=injectBusy;
  const int16_t pcm[]={1000,-1000,2000}; playPcm(reinterpret_cast<const uint8_t*>(pcm),sizeof(pcm));
  assertBusyExit(); host::onI2sWrite={};
  require(host::playedSamples==std::vector<int16_t>({400,400,-400,-400,800,800}),"real PCM code must duplicate mono channels at 40 percent volume");
  require(host::i2sConfig.gpio_cfg.bclk==26 && host::i2sConfig.gpio_cfg.ws==25 && host::i2sConfig.gpio_cfg.dout==27,"audio output must use original I2S pins");
  rearm(); key('s'); require(letterIdx==1,"input must recover after playback");
}
void pcm_creation_failure_restores_gate() {
  ready(); host::i2sCreateResult=-1; const int16_t pcm[]={1000};
  playPcm(reinterpret_cast<const uint8_t*>(pcm),sizeof(pcm)); assertBusyExit();
  rearm(); key('s'); require(letterIdx==1,"early I2S failure must release busy gate");
}
void setup_offline_nonblocking_single_display() {
  Wire.reset(); host::setMillis(0); host::wifiStatus=WL_DISCONNECTED;
  host::onWifiBegin=[] { require(gesture::mpuReady && gesture::samplingReady,"sensors and calibration sampling must start before WiFi"); };
  ::setup();
  require(millis()<2000,"offline setup must not wait for WiFi to connect");
  require(host::oledConstructs==1 && host::oledBegins==1,"merged application must construct and initialize only one OLED");
  require(&u8g2==&gesture::display,"business UI must reuse the sensor OLED object");
  require(Wire.sda==21 && Wire.scl==22,"I2C pin assignments must remain unchanged");
  require(host::httpRequests.empty(),"startup must not make cloud requests");
  gesture::sharedEmg.worn=true; gesture::sharedEmg.warmed=true; gesture::sharedEmg.raw=512;
  gesture::sharedEmg.sequence=1; gesture::sharedEmg.timeUs=micros(); gesture::sharedEmg.newEnv=2;
  Serial.feed("c"); frame(); require(gesture::collecting,"offline boot must still accept calibration commands");
}
void sampler_uses_only_approved_pins() {
  fixture(); host::stopSampler=true;
  try { gesture::sampleTask(nullptr); } catch(const host::SamplingStopped&) {}
  require(host::analogPins==std::vector<int>({34}),"actual sampling task must read EMG only from GPIO34");
  require(host::digitalPins==std::vector<int>({2}),"actual sampling task must read detect from GPIO2");
  require(host::digitalWrites.size()==3 && host::digitalWrites[0].first==5 && host::digitalWrites[1].first==18 && host::digitalWrites[2].first==19,"sampling indicators must retain GPIO5/18/19");
}
void selected_sentence_runs_full_playback_chain() {
  ready(); state=S_CAND; buf="ni'hao"; cands[0]="你好"; candCount=1; candIdx=0;
  host::onHttp=injectBusy; host::onI2sWrite=injectBusy;
  host::reply(200,R"({"output":{"audio":{"url":"https://audio.invalid/selected.pcm"}}})");
  host::reply(200,"",{0xE8,0x03,0x18,0xFC});
  key(' ');
  require(host::httpRequests.size()==2,"selected confirmation must request TTS then download once each");
  require(host::httpRequests[0].method=="POST" && host::httpRequests[0].body.find("你好")!=std::string::npos,"TTS request must carry the selected sentence");
  require(host::httpRequests[1].method=="GET" && host::httpRequests[1].url=="https://audio.invalid/selected.pcm","download must use the parsed TTS URL");
  require(host::playedSamples==std::vector<int16_t>({400,400,-400,-400}),"downloaded PCM must actually reach the original output conversion");
  require(state==S_PINYIN && buf=="" && letterIdx==0,"completed confirmation must restore original input page");
  require(busyDepth==0 && !inputRouter.busy(),"nested busy guards must all unwind after complete playback");
  assertBusyExit(); host::onHttp={}; host::onI2sWrite={};
  hold('w',80); require(letterIdx==0,"post-playback held posture must not replay");
  rearm(); key('s'); require(letterIdx==1,"full playback must allow input only after neutral recovery");
}
void download_failure_and_busy_commands_are_discarded() {
  ready(); state=S_CAND; buf="ni"; cands[0]="你好"; candCount=1;
  host::onHttp=[] { injectBusy(); Serial.feed("cudrlg"); ::loop(); };
  host::reply(200,R"({"output":{"audio":{"url":"https://audio.invalid/fail.pcm"}}})");
  host::reply(503);
  key(' ');
  require(host::httpRequests.size()==2 && host::playedSamples.empty(),"failed download must not play audio");
  require(gesture::calibration==gesture::Calibration::Complete && !gesture::collecting,"calibration bytes received while busy must not restart calibration");
  require(busyDepth==0 && !inputRouter.busy(),"download error must unwind all nested busy levels");
  assertBusyExit(); host::onHttp={}; rearm(); key('s'); require(letterIdx==1,"input must recover from nested download failure");
}
void manual_burst_does_not_bypass_rearm() {
  ready(); Serial.feed("s fw"); frame();
  require(letterIdx==1 && buf=="","only first manual action may execute before another neutral recovery");
  require(Serial.available()==0,"discarded manual burst must not remain for later replay");
  rearm(); frame(); require(letterIdx==1 && buf=="","rearm must not replay old serial bytes");
}
void missing_key_never_starts_cloud_request() {
  ready(); API_KEY=""; buf="ni"; key('x');
  require(state==S_PINYIN && buf=="ni","missing API key must preserve input for retry");
  require(!getCandidates("ni") && tts("你好")=="","direct helpers must reject missing credentials too");
  require(host::httpBegins==0 && host::httpRequests.empty(),"empty local credentials must never create HTTP requests");
}
void empty_wifi_configuration_still_allows_calibration() {
  Wire.reset(); host::setMillis(0); WIFI_SSID=""; API_KEY=""; ::setup();
  require(host::wifiBegins==0 && host::httpBegins==0,"unconfigured startup must not initiate network connections");
  gesture::sharedEmg.worn=true; gesture::sharedEmg.warmed=true;
  gesture::sharedEmg.raw=512; gesture::sharedEmg.sequence=1; gesture::sharedEmg.newEnv=2;
  gesture::sharedEmg.timeUs=micros(); Serial.feed("c"); frame();
  require(gesture::collecting && gesture::captureKind=='c',"blank local config must not block calibration");
}
// Drive the actual 500Hz sampler and control loop, including contact faults between frames.
void sampleContactTrace(int kind) {
  fixture(); calibrate();
  const uint32_t started=micros();
  host::stopSampler=false; host::digitalValue=HIGH;
  host::onSampleDelay=[&] {
    const uint32_t elapsed=(micros()-started)/1000u;
    char pose='n';
    if(elapsed>=2310 && (kind!=2 || elapsed<3100 || elapsed>=3800)) pose='w';
    sampleRegisters(direction(pose));
    if(elapsed%20==0) gesture::controlStep();
    if(elapsed>=2300 && elapsed<2360) {
      require(gesture::eventCount==0 && letterIdx==0,"uncertain contact must never dispatch any operation");
      require(!inputHealthy(),"raw contact dropout and settling must immediately block the application gate");
    }
    if(kind==1 && elapsed==2420)
      require(!gesture::sharedEmg.worn,"continuous low contact must be confirmed absent");
    if(kind==2 && elapsed==3000)
      require(gesture::eventCount==0,"prolonged contact chatter must require neutral recovery");
    if(kind==2 && elapsed==3700)
      require(gesture::recognizer.ready(),"持续回位需包含姿态平滑后的完整放松等待时间");
    const unsigned end=kind==2?4300:2900;
    if(elapsed>=end) throw host::SamplingStopped();
    host::nowUs+=2000;
    const uint32_t next=elapsed+2;
    const bool low=kind==0 ? (next>=2300 && next<2306) :
        kind==1 ? (next>=2300 && next<2600) :
        (next>=2300 && next<2900 && ((next-2300)/20)%2==0);
    host::digitalValue=low?LOW:HIGH;
  };
  try { gesture::sampleTask(nullptr); } catch(const host::SamplingStopped&) {}
  host::onSampleDelay={};
}
void brief_contact_dropout_preserves_direction() {
  sampleContactTrace(0);
  require(gesture::eventCount==1 && gesture::lastKey=='w' && letterIdx==25,
      "6ms contact dip during up motion must pause then permit one fresh stable direction");
  require(gesture::sharedEmg.wearLosses==0,"brief contact dip must not become confirmed detachment");
}
void sustained_contact_loss_still_locks() {
  sampleContactTrace(1);
  require(gesture::eventCount==0 && letterIdx==0,"holding up after real detachment must not replay");
  require(gesture::sharedEmg.wearLosses==1,"one continuous detachment is one confirmed loss");
  hold('n',70); hold('w',40);
  require(letterIdx==25,"real detachment requires recovery to neutral before the next action");
}
void repeated_contact_chatter_cannot_bypass_lock() {
  sampleContactTrace(2);
  require(gesture::sharedEmg.wearLosses==1,"long alternating contact must eventually confirm a fault");
  require(gesture::eventCount==1 && letterIdx==25,"after chatter only the post-neutral direction may execute");
}
void early_emg_rise_does_not_swallow_up() {
  ready(); hold('n',10,20);
  for(unsigned i=0;i<5;++i) {
    host::advanceMillis(20); ++gesture::sharedEmg.sequence; gesture::sharedEmg.timeUs=micros();
    sampleRegisters(direction('w'),3930); gesture::controlStep();
  }
  hold('w',40,20);
  require(letterIdx==25 && gesture::eventCount==1,"natural wrist effort before up must not swallow direction");
  require(buf=="" && host::httpRequests.empty(),"cancelled grip must neither confirm nor send text");
}
void screen_distinguishes_motion_from_wear() {
  ready(); gesture::liveStable=false; host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"正在移动请停稳"},"motion must not be labelled missing electrodes");
}
// 姿态读数本身无效不能提示用户只是正在移动。
void screen_distinguishes_invalid_pose_from_motion() {
  ready(); host::advanceMillis(20);
  ++gesture::sharedEmg.sequence; gesture::sharedEmg.timeUs=micros();
  sampleRegisters({0,0,0}); gesture::controlStep();
  require(!inputHealthy() && !gesture::recognizer.ready(),"无效姿态必须暂停输入并锁定");
  host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"姿势暂不可用"},"坏姿态数据不能误提示为移动");
}
void screen_distinguishes_return_and_release() {
  ready(); gesture::recognizer.reset(); hold('w',30);
  host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"请回到自然位置"},"locked non-neutral pose needs a specific return hint");
  hold('n',60,20); host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"请松开握拳"},"neutral high EMG needs a release hint rather than return");
}
void screen_distinguishes_emg_fault_from_wear() {
  ready(); ++gesture::sharedEmg.faults; host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"肌电数据异常"},"EMG fault must not be labelled missing electrodes");
  gesture::sharedEmg.worn=false; host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"未佩戴请检查电极"},"actual detachment must remain visible");
}
// 采样线程先看到接触变化时，不能复用控制帧留下的输入许可。
void latest_contact_snapshot_blocks_serial() {
  ready(); gesture::sharedEmg.wearUncertain=true;
  Serial.feed("w "); gesture::receiveCommands();
  require(letterIdx==0 && buf=="","接触不确定立即阻止串口输入");
  gesture::sharedEmg.wearUncertain=false; ++gesture::sharedEmg.wearPauses;
  Serial.feed("w "); gesture::receiveCommands();
  require(letterIdx==0 && buf=="","两控制帧之间已结束的接触抖动仍必须被处理");
}
void screen_and_status_distinguish_contact_pause() {
  ready(); gesture::sharedEmg.wearUncertain=true; ++gesture::sharedEmg.wearPauses;
  host::screenText.clear(); drawInputHint();
  require(host::screenText==std::vector<std::string>{"接触不稳暂停操作"},"短暂接触不稳不能提示真正脱落");
  Serial.clearOutput(); gesture::printStatus();
  require(Serial.output.find("确认脱落次数=0")!=std::string::npos &&
      Serial.output.find("接触暂停次数=1")!=std::string::npos,"诊断分开报告接触暂停与确认脱落次数");
}
void calibration_rejects_uncertain_contact() {
  fixture(); gesture::beginCalibration('c');
  host::setMillis(gesture::captureStartedMs+3000);
  gesture::sharedEmg.wearUncertain=true; ++gesture::sharedEmg.wearPauses;
  frame('n',2,false);
  require(gesture::collecting && gesture::captureCount==0 && gesture::skippedCount==1,
      "校准不能接收接触不确定样本");
  Serial.clearOutput(); gesture::printCalibrationDiagnostics();
  require(Serial.output.find("接触不稳=1")!=std::string::npos &&
      Serial.output.find("其他输入异常=0")!=std::string::npos,"校准诊断明确标明接触不稳");
  gesture::sharedEmg.wearUncertain=false; frame('n',2,false);
  require(gesture::captureCount==1 && gesture::skippedCount==1,"恢复后的有效校准样本正常接收");
}
void contact_pause_with_real_fault_still_locks() {
  ready(); gesture::sharedEmg.wearUncertain=true; ++gesture::sharedEmg.wearPauses;
  ++gesture::sharedEmg.faults; frame('n',2,false);
  gesture::sharedEmg.wearUncertain=false; hold('w',40);
  require(gesture::eventCount==0 && letterIdx==0,"接触抖动伴随真实坏数据必须锁定");
  rearm(); hold('w',40);
  require(gesture::eventCount==1 && letterIdx==25,"真实故障恢复后需先回位");
}
// 接触暂停只能容忍接触抖动，不能掩盖同一帧的真实传感器故障。
void pausedSensorFault(int kind) {
  ready(); gesture::sharedEmg.wearUncertain=true; ++gesture::sharedEmg.wearPauses;
  host::advanceMillis(20); ++gesture::sharedEmg.sequence; gesture::sharedEmg.timeUs=micros();
  sampleRegisters(kind==0 ? Vec3{0,0,0} : direction('n'));
  if(kind==1) Wire.queueFailure(0x3A);
  if(kind==2) gesture::sharedEmg.newEnv=-1;
  gesture::controlStep();
  require(!inputHealthy(),"接触暂停与坏数据同时发生时不得接收输入");
  gesture::sharedEmg.wearUncertain=false; hold('w',40);
  require(gesture::eventCount==0 && letterIdx==0,"接触恢复不能掩盖此前的真实传感器故障");
  rearm(); hold('w',40);
  require(gesture::eventCount==1 && letterIdx==25,"真实故障后回位放松才恢复方向操作");
}
void contact_pause_with_zero_pose_still_locks() { pausedSensorFault(0); }
void contact_pause_with_mpu_read_failure_still_locks() { pausedSensorFault(1); }
void contact_pause_with_negative_envelope_still_locks() { pausedSensorFault(2); }
struct Case { const char* name; void (*run)(); };
const Case cases[]={
#define CASE(x) {#x,x}
  CASE(uncalibrated_serial_is_blocked), CASE(calibrated_up_changes_letter),
  CASE(real_six_actions_reach_business), CASE(serial_six_keys_reach_business),
  CASE(same_frame_bad_emg_blocks_serial), CASE(same_frame_mpu_failure_blocks_serial),
  CASE(changed_snapshot_blocks_serial), CASE(stale_snapshot_blocks_serial),
  CASE(lost_wear_blocks_serial), CASE(unstable_pose_blocks_serial),
  CASE(llm_success_discards_busy_input), CASE(llm_http_failure_restores_gate),
  CASE(llm_invalid_json_restores_gate), CASE(tts_failure_restores_gate),
  CASE(tts_success_uses_real_json), CASE(wifi_offline_local_only),
  CASE(pcm_playback_discards_busy_input), CASE(pcm_creation_failure_restores_gate),
  CASE(setup_offline_nonblocking_single_display), CASE(sampler_uses_only_approved_pins),
  CASE(selected_sentence_runs_full_playback_chain), CASE(download_failure_and_busy_commands_are_discarded),
  CASE(manual_burst_does_not_bypass_rearm), CASE(missing_key_never_starts_cloud_request),
  CASE(empty_wifi_configuration_still_allows_calibration),
  CASE(brief_contact_dropout_preserves_direction), CASE(sustained_contact_loss_still_locks),
  CASE(repeated_contact_chatter_cannot_bypass_lock), CASE(early_emg_rise_does_not_swallow_up),
  CASE(screen_distinguishes_motion_from_wear), CASE(screen_distinguishes_invalid_pose_from_motion),
  CASE(screen_distinguishes_return_and_release),
  CASE(screen_distinguishes_emg_fault_from_wear), CASE(latest_contact_snapshot_blocks_serial),
  CASE(screen_and_status_distinguish_contact_pause), CASE(calibration_rejects_uncertain_contact),
  CASE(contact_pause_with_real_fault_still_locks), CASE(contact_pause_with_zero_pose_still_locks),
  CASE(contact_pause_with_mpu_read_failure_still_locks), CASE(contact_pause_with_negative_envelope_still_locks)
#undef CASE
};
}
int main(int argc,char** argv) {
  if(argc==2 && std::string(argv[1])=="--list") { for(const auto& c:test::cases) std::cout<<c.name<<'\n'; return 0; }
  if(argc==2) {
    for(const auto& c:test::cases) if(std::string(argv[1])==c.name) {
      try { c.run(); std::cout<<"PASS "<<c.name<<'\n'; return 0; }
      catch(const std::exception& e) { std::cerr<<"FAIL "<<c.name<<": "<<e.what()<<'\n'; return 1; }
    }
    std::cerr<<"Unknown test case\n"; return 2;
  }
  unsigned failures=0;
  for(const auto& c:test::cases) {
#ifdef _WIN32
    const intptr_t status=_spawnl(_P_WAIT,argv[0],argv[0],c.name,static_cast<char*>(nullptr));
#else
    const pid_t child=fork();
    if(child==0) { execl(argv[0],argv[0],c.name,static_cast<char*>(nullptr)); _exit(127); }
    int result=0; waitpid(child,&result,0); const int status=WIFEXITED(result)?WEXITSTATUS(result):1;
#endif
    if(status!=0) ++failures;
  }
  std::cout<<(sizeof(test::cases)/sizeof(test::cases[0])-failures)<<"/"<<sizeof(test::cases)/sizeof(test::cases[0])<<" passed\n";
  return failures?1:0;
}
