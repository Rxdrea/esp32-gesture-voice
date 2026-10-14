#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
void serviceConfigPortal() {}
#include "GestureVoice.ino"
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct Frame{float a[7];int worn=1;};
Frame rest={{0,0,1,0,0,0,2},1};
void frame(const Frame& f){
 for(unsigned k=0;k<7;++k){int v=k==3?0:int(std::lround(f.a[k<3?k:k-1]*(k<3?16384:131)));v=std::max(-32768,std::min(32767,v));Wire.registers[0x3B+2*k]=uint16_t(v)>>8;Wire.registers[0x3C+2*k]=uint16_t(v)&255;}
 gesture::sharedEmg={f.a[6],bool(f.worn),false,true,micros(),gesture::sharedEmg.sequence+1};
 ::loop();host::advanceMillis(9);
}
void quiet(unsigned n=600){for(unsigned i=0;i<n;++i)frame(rest);}
unsigned count(const std::string& s,const std::string& key){unsigned n=0;size_t p=0;while((p=s.find(key,p))!=std::string::npos){++n;p+=key.size();}return n;}
void fixture(){Wire.reset();host::setMillis(100);gesture::mpuReady=true;gesture::samplingReady=true;host::wifiStatus=WL_CONNECTED;quiet();Serial.clearOutput();}
void manual(char c){manualReady=true;dispatchAction(c,false);}
void replies(){host::reply(200,R"({"choices":[{"message":{"content":"[\"你好\",\"您好\"]"}}]})");}
bool utf8(const char* s){const auto* p=(const unsigned char*)s;while(*p){unsigned n=*p<128?1:(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:(*p&0xf8)==0xf0?4:0;if(!n)return false;for(unsigned i=1;i<n;++i)if(!p[i]||(p[i]&0xc0)!=0x80)return false;p+=n;}return true;}

Frame tilt(float degree){
 const float a[]={.94614074f,-.13719897f,.29324758f};const float t=degree/57.29578f,s=std::sin(t),c=std::cos(t);
 Frame f={{a[1]*s+a[0]*a[2]*(1-c),-a[0]*s+a[1]*a[2]*(1-c),c+a[2]*a[2]*(1-c),0,0,0,2},0};return f;
}
void scrollCase(const std::string& name){
 rest.worn=0;quiet();Serial.clearOutput();
 if(name=="scroll_jitter" || name=="scroll_side"){
  Frame f=tilt(30);if(name=="scroll_side"){f.a[0]=.94f;f.a[1]=0;f.a[2]=.342f;}
  for(int i=0;i<250;++i)frame(f);quiet();check(count(Serial.output,"【连续选择】")==0,"grip-sized tilt or side tilt scrolled");return;
 }
 if(name=="scroll_candidates"){state=S_CAND;candCount=3;candIdx=1;}
 bool up=name=="scroll_up";float angle=up?-55.f:55.f;
 uint32_t begin=millis(),first=0;
 for(int i=1;i<=20;++i){Frame f=tilt(angle*i/20);f.a[3]=up?-180:180;frame(f);}
 for(int i=0;i<220;++i){frame(tilt(angle));if(!first && count(Serial.output,"【连续选择】"))first=millis();}
 unsigned steps=count(Serial.output,"【连续选择】");
 check(steps>=4,"held pose did not repeat");check(first-begin<=1000,"fast path still waits for two-second model");
 check(count(Serial.output,"【模型操作】")==0,"old model also dispatched held scroll");
 check(count(Serial.output,"【输入结果】")==steps,"scroll events not applied once each");
 if(name=="scroll_candidates")check(candIdx==int((1+steps)%3)&&letterIdx==0,"candidate repeat mapping");
 else check(letterIdx==int(up?(26-steps%26)%26:steps%26),"continuous letter index");
 if(name=="scroll_partial_release"){for(int i=0;i<200;++i)frame(tilt(35));unsigned after=count(Serial.output,"【连续选择】");check(after>steps,"held reduced angle did not continue scrolling");steps=after;}
 if(name=="scroll_sensor_gap"){host::advanceMillis(100);for(int i=0;i<200;++i)frame(tilt(angle));}
 if(name=="scroll_direction_change")for(int i=0;i<200;++i)frame(tilt(-angle));
 frame(rest);quiet();check(count(Serial.output,"【连续选择】")==steps,"release continued scrolling");check(host::httpRequests.empty(),"scroll triggered association");
 std::cout<<"first_ms="<<first-begin<<" steps="<<steps<<" release_stopped=1\n";
}
void llmErrorCase(const std::string& name){
 int status=200;std::string body,expected;
 if(name=="http_connect"){status=-1;expected="云服务连接失败";}
 else if(name=="http_timeout"){status=-11;expected="云服务响应超时";}
 else if(name=="http_400"){status=400;expected="联想请求参数错误";}
 else if(name=="http_401"){status=401;expected="密钥或服务区域不匹配";}
 else if(name=="http_403"){status=403;expected="云服务权限不足";}
 else if(name=="http_429"){status=429;expected="额度或调用次数受限";}
 else if(name=="http_500"){status=500;expected="云服务暂时异常";}
 else if(name=="http_arrearage"){status=429;body=R"({"error":{"code":"Arrearage","message":"not printed"}})";expected="云服务账户欠费";}
 else if(name=="http_invalid_key"){status=401;body=R"({"error":{"code":"InvalidApiKey","message":"not printed"}})";expected="密钥或服务区域不匹配";}
 else if(name=="http_free_quota"){status=429;body=R"({"error":{"code":"AllocationQuota.FreeTierOnly","message":"not printed"}})";expected="免费额度已用完";}
 else if(name=="http_unsafe_code"){status=429;body=R"({"error":{"code":"sk-test-only","message":"do-not-print-body"}})";expected="额度或调用次数受限";}
 else if(name=="http_bad_json"){body="{";expected="服务响应解析失败";}
 else if(name=="http_empty"){body=R"({"choices":[{"message":{"content":"[]"}}]})";expected="没有返回中文候选";}
 else if(name=="http_bad_content"){body=R"({"choices":[{"message":{"content":"不是JSON"}}]})";expected="中文候选格式错误";}
 else if(name=="http_object"){body=R"({"choices":[{"message":{"content":"{}"}}]})";expected="中文候选不是列表";}
 else if(name=="http_error_200"){body=R"({"error":{"code":"Arrearage"}})";expected="云服务账户欠费";}
 else if(name=="http_codeblock"){body=R"({"choices":[{"message":{"content":"```json\n[\"你好\"]\n```"}}]})";}
 else throw std::runtime_error("unknown HTTP test");
 buf="ni";host::reply(status,body);commitWord();
 check(host::httpRequests.size()==1,"HTTP duplicated or missing");check(busyDepth==0&&!inputRouter.busy(),"failure retained busy");check(buf=="ni","failed association erased pinyin");
 if(name=="http_codeblock")check(state==S_CAND&&candCount==1,"valid fenced JSON rejected");
 else {check(state==S_PINYIN&&candCount==0,"failed response selected candidates");check(std::string(llmErrorHint)==expected,"wrong visible failure reason");check(lastLlmHttpStatus==status,"HTTP code lost");}
 check(Serial.output.find("do-not-print-body")==std::string::npos&&Serial.output.find("sk-test-only")==std::string::npos,"raw error leaked");
}

void leds(int mask){check(host::digitalWrites.size()>=3,"missing LED writes");auto n=host::digitalWrites.size();for(int i=0;i<3;++i)check(host::digitalWrites[n-3+i]==std::make_pair(int(gesture::LED_PINS[i]),(mask>>i)&1),"LED output mismatch");}
void sampleCase(const std::string& c){
 host::setMillis(0);host::digitalValue=c=="sample_unworn"?LOW:HIGH;host::analogValue=15;unsigned ticks=0;bool stages[5]={};
 host::onSampleDelay=[&](){++ticks;auto e=gesture::emgSnapshot();check(e.sequence==ticks&&e.timeUs==micros(),"sampler did not publish real samples");
 if(millis()<1500){leds(0);check(!e.warmed,"premature warmup");}
 if(millis()==1498)check(e.worn==(c!="sample_unworn"),"wear filtering");
 if(millis()>=1500){check(e.warmed,"warmup not complete");int raw=host::analogValue;int mask=c=="sample_unworn"?0:(raw>=6?1:0)|(raw>=9?2:0)|(raw>=12?4:0);leds(mask);if(raw==5)stages[0]=true;if(raw==6)stages[1]=true;if(raw==9)stages[2]=true;if(raw==12)stages[3]=true;if(raw==15)stages[4]=true;
 if(c=="sample_led"){if(ticks%2==0)host::analogValue=raw==15?5:raw==5?6:raw==6?9:raw==9?12:15;}}
 host::advanceMillis(2);if(ticks>=800)throw host::SamplingStopped();};
 try{gesture::emgSampleTask(nullptr);}catch(const host::SamplingStopped&){}host::onSampleDelay={};check(ticks==800,"sampling loop incomplete");if(c=="sample_led")for(bool b:stages)check(b,"missing LED threshold stage");check(gesture::emgSnapshot().worn==(c!="sample_unworn"),"unworn state");
}

std::vector<Frame> resetTrace;
void resetCore(){fusion::Core a,b;uint32_t tm=1000;fusion_features::Sample rest={0,0,1,0,0,0,2};
 auto quietCore=[&](fusion::Core& x){for(int i=0;i<150;++i){x.update(tm,rest,true,true);tm+=10;}};
 auto replay=[&](fusion::Core& x){unsigned n=0;for(auto f:resetTrace){fusion_features::Sample s={f.a[0],f.a[1],f.a[2],f.a[3],f.a[4],f.a[5],f.a[6]};n+=x.update(tm,s,true,true);tm+=10;}return n;};
 quietCore(a);check(a.neutralReady&&a.armed,"old reference missing");check(replay(a)==1,"old grip trace not recognized");a.reset();check(!a.neutralReady&&!a.armed&&a.count==0&&a.events==0&&a.resets==0&&a.fault==0&&a.above10==0&&a.gripProbability==0&&a.motionProbability==0&&a.rate==0&&a.angle==0&&!a.motionDecisionFresh(tm),"Core reset retained state");tm+=10000;
 for(int i=0;i<150;++i){check(a.update(tm,rest,true,true)==b.update(tm,rest,true,true),"reset diverges from fresh");check(a.count==b.count&&a.angle==b.angle&&a.gripProbability==b.gripProbability&&a.motionProbability==b.motionProbability&&a.fault==b.fault,"old Core data leaked");if(i<99)check(!a.neutralReady,"old window reused");tm+=10;}check(a.neutralReady&&a.armed,"new reference missing");check(replay(a)==1&&a.events==1,"same Core failed recognition after reset");}
void resetWrist(){WristMotionGate a,b;uint32_t tm=1000;float out[6][100];
 auto rawOf=[](const Frame& f,int16_t* raw){for(int k=0;k<7;++k){float v=k==3?0:f.a[k<3?k:k-1]*(k<3?16384:131);raw[k]=int16_t(std::max(-32768.f,std::min(32767.f,v)));}};
 bool confirmed=false,seeded=false;for(auto f:resetTrace){int16_t raw[7];rawOf(f,raw);a.update(tm,raw);confirmed|=a.confirmed(tm);seeded|=a.seed(out[0],out[1],out[2],out[3],out[4],out[5])>0;tm+=10;}check(confirmed&&seeded,"old motion not established");a.reset();check(!a.decisionFresh(tm)&&!a.confirmed(tm)&&a.probability==0&&a.seed(out[0],out[1],out[2],out[3],out[4],out[5])==0,"wrist reset retains data");tm+=10000;
 int16_t resting[7]={0,0,16384,0,0,0,0};for(int i=0;i<110;++i){a.update(tm,resting);b.update(tm,resting);check(a.probability==b.probability&&a.confirmed(tm)==b.confirmed(tm)&&a.decisionFresh(tm)==b.decisionFresh(tm),"old wrist state leaked");check(a.seed(out[0],out[1],out[2],out[3],out[4],out[5])==0,"old motion seed retained");if(i<99)check(!a.decisionFresh(tm),"old wrist samples reused");tm+=10;}
 confirmed=false;for(auto f:resetTrace){int16_t raw[7];rawOf(f,raw);a.update(tm,raw);b.update(tm,raw);check(a.probability==b.probability&&a.confirmed(tm)==b.confirmed(tm),"new wrist recognition differs from fresh");confirmed|=a.confirmed(tm);tm+=10;}check(confirmed,"same wrist instance failed after reset");}
int main(int argc,char** argv){try{check(argc>=2,"case required");std::string c=argv[1];
 if(c=="setup"){check(Wire.registers.empty(),"setup must start with empty Wire");::setup();check(gesture::samplingReady&&host::sampleTaskFn==gesture::emgSampleTask&&host::taskName=="EMG500Hz"&&host::taskStack==4096&&host::taskPriority==2&&host::taskCore==1,"startup sampler task");for(auto pin:gesture::LED_PINS){check(std::find(host::pinModes.begin(),host::pinModes.end(),std::make_pair(int(pin),OUTPUT))!=host::pinModes.end(),"LED not OUTPUT");check(std::find(host::digitalWrites.begin(),host::digitalWrites.end(),std::make_pair(int(pin),LOW))!=host::digitalWrites.end(),"LED not initially LOW");}check(gesture::mpuReady&&Wire.sda==21&&Wire.scl==22,"startup MPU/I2C");check(std::find(host::digitalPins.begin(),host::digitalPins.end(),33)!=host::digitalPins.end(),"GPIO33 startup diagnostic absent");std::cout<<Serial.output<<"PASS setup (host; ESP32 AP/preferences/restart branches excluded)\n";return 0;}
 if(c.rfind("sample_",0)==0){sampleCase(c);std::cout<<"PASS "<<c<<"\n";return 0;}
 if(c=="reset_core"||c=="reset_wrist"){check(argc==3,"reset trace required");std::ifstream in(argv[2]);unsigned n;in>>n;resetTrace.resize(n);for(auto& f:resetTrace){for(float& v:f.a)in>>v;in>>f.worn;}check(bool(in),"reset trace parse");if(c=="reset_core")resetCore();else resetWrist();std::cout<<"PASS "<<c<<"\n";return 0;}fixture();
 if(c.rfind("scroll_",0)==0){scrollCase(c);}
 else if(c.rfind("http_",0)==0){llmErrorCase(c);}
 else if(c=="mapping"){check(gesture::actionKey("up")=='w'&&gesture::actionKey("down")=='s'&&gesture::actionKey("clockwise")=='f'&&gesture::actionKey("counterclockwise")=='b'&&gesture::actionKey("right_side")==' '&&gesture::actionKey("rest")==0,"mapping");}
 else if(c=="utf8"){std::string a;for(int i=0;i<80;++i)a+="中文a";gesture::queueScreen(a.c_str(),a.c_str(),a.c_str(),a.c_str(),a.c_str());auto v=gesture::screenVersion;gesture::queueScreen(a.c_str(),a.c_str(),a.c_str(),a.c_str(),a.c_str());check(v==gesture::screenVersion,"same normalized text increments version");for(auto& row:gesture::screen)check(utf8(row),"truncated UTF8");}
 else if(c=="unauthorized"){dispatchAction('s',true);check(letterIdx==0,"unauthorized model dispatch");}
 else if(c=="no_wear_x"){rest.worn=0;quiet();buf="ni";manual('x');check(host::httpRequests.empty()&&buf=="ni","unworn x performed HTTP");manual('s');check(letterIdx==1,"unworn wrist blocked");}
 else if(c=="busy"){beginBusyInput();dispatchAction('s',false);dispatchAction('s',true);check(letterIdx==0,"busy executed input");endBusyInput();quiet();check(letterIdx==0&&host::httpRequests.empty(),"busy replayed");}
 else if(c=="candidate_http"){buf="ni";replies();host::onHttp=[](){dispatchAction('s',true);dispatchAction('s',false);};manual('x');check(state==S_CAND&&candCount==2&&host::httpRequests.size()==1&&candIdx==0,"candidate chain");host::onHttp={};quiet();check(candIdx==0&&host::httpRequests.size()==1,"candidate busy replay");}
 else if(c=="speech"){state=S_CAND;cands[0]="你好";candCount=1;host::reply(200,R"({"output":{"audio":{"url":"https://audio.invalid/test.pcm"}}})");host::reply(200,"",{0xe8,3,0x18,0xfc});manual(' ');check(host::httpRequests.size()==2&&host::playedSamples==std::vector<int16_t>({400,400,-400,-400})&&state==S_PINYIN,"speech chain");quiet();check(host::httpRequests.size()==2&&buf=="","speech replay");}
 else if(c=="replay" || c=="unworn" || c=="cand_up" || c=="cand_down" || c=="cand_confirm"){
 std::string label=argv[2];std::ifstream in(argv[3]);unsigned n;in>>n;std::vector<Frame> fs(n);for(auto& f:fs){for(float& v:f.a)in>>v;in>>f.worn;}check(bool(in),"trace parse");
 if(label=="counterclockwise")buf="ni";if(label=="clockwise")buf="ni";if(label=="grip"){buf="ni";replies();}
  if(c=="unworn"){rest.worn=0;quiet();for(auto& f:fs)f.worn=0;}
 if(c=="cand_up"||c=="cand_down"||c=="cand_confirm"){state=S_CAND;candCount=3;candIdx=1;cands[0]="你好";cands[1]="您好";cands[2]="再见";buf="ni";}
 if(c=="cand_confirm"){host::reply(200,R"({"output":{"audio":{"url":"https://audio.invalid/model.pcm"}}})");host::reply(200,"",{0xe8,3,0x18,0xfc});host::onHttp=[](){dispatchAction('s',false);dispatchAction('s',true);};}
 for(auto& f:fs)frame(f);quiet();unsigned modelEv=count(Serial.output,"【模型操作】"),fastEv=count(Serial.output,"【连续选择】");unsigned ev=modelEv+fastEv;unsigned feedback=count(Serial.output,"【输入结果】");
 std::cout<<"model_events="<<modelEv<<" fast_events="<<fastEv<<" ";
 std::cout<<"events="<<ev<<" feedback="<<feedback<<" letter="<<letterIdx<<" buf="<<buf<<" state="<<int(state)<<" HTTP="<<host::httpRequests.size()<<" confidence="<<gesture::resultConf<<"\n";
  if(c=="cand_up"){check(ev==1&&feedback==1&&candIdx==0,"model candidate up");}
 else if(c=="cand_down"){check(ev==1&&feedback==1&&candIdx==2,"model candidate down");}
 else if(c=="cand_confirm"){check(ev==1&&feedback==1&&host::httpRequests.size()==2&&host::playedSamples==std::vector<int16_t>({400,400,-400,-400})&&state==S_PINYIN&&buf=="","model confirm speech/busy once");}
 else if(c=="unworn"&&label=="grip"){check(ev==0&&feedback==0&&host::httpRequests.empty()&&state==S_PINYIN,"unworn grip network");}
 else if(label=="rest")check(ev==0&&letterIdx==0&&buf=="","rest false activation");
 else {check(ev==1,"expected exactly one model operation");check(feedback==1,"expected exactly one successful dispatch feedback");if(label=="up")check(letterIdx==25,"up business");if(label=="down")check(letterIdx==1,"down business");if(label=="clockwise")check(buf=="ni'","separator business");if(label=="counterclockwise")check(buf=="n","backspace business");if(label=="right_side")check(buf=="a","confirm business");if(label=="grip")check(state==S_CAND&&candCount==2&&host::httpRequests.size()==1,"grip candidates business");}
 }
 else throw std::runtime_error("unknown case");std::cout<<"PASS "<<c<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
