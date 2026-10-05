#pragma once
/**
 * 手势语音识别版1.0：个性化姿势采集、校准和诊断。
 * 肌电S接GPIO34（ADC1），L接GPIO2；灯接5/18/19。
 * OLED与MPU6050共用SDA21/SCL22，I2C只由主循环访问。
 * MAX98357：BCLK26、LRC25、DIN27；蓝牙模块串口预留RX16/TX17。
 * 校准依次c/u/d/r/l/g；五个姿势独立学习，不要求固定倾角或对称。
 * 动作交给主程序统一处理；网络/播报期间不识别、不缓存动作。
 * 断电需重新校准。持续肌电低值仍可能被视为松开，须实物验证。
 * 本程序不是医疗或紧急通信设备。
 */
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <CheezsEMG.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "GestureCore.h"
#include <Preferences.h>

#if !defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_FREERTOS_UNICORE)
#error "请选择经典双核ESP32开发板：ESP32 Dev Module（ESP32-WROOM-32）。"
#endif
namespace gesture {
using sixops::Vec3;
constexpr uint8_t INPUT_PIN=34, DETECT_PIN=2;
constexpr uint8_t BLUETOOTH_RX_PIN=16, BLUETOOTH_TX_PIN=17;
constexpr uint8_t EXTRA_SERIAL_RX_PIN=4, EXTRA_SERIAL_TX_PIN=13;
constexpr uint8_t RESET_BUTTON_PIN=33;
constexpr uint32_t RESET_HOLD_MS=1000;
constexpr uint8_t LED_PINS[]={5,18,19};
constexpr int LED_THRESHOLDS[]={6,9,12};
constexpr uint32_t SAMPLE_RATE=500, CONTROL_MS=20;
constexpr TickType_t SAMPLE_TICKS=pdMS_TO_TICKS(2);
static_assert(configTICK_RATE_HZ>=SAMPLE_RATE && configTICK_RATE_HZ%SAMPLE_RATE==0,
              "系统时钟必须支持每2毫秒采样一次。");
constexpr size_t CAL_POINTS=100;
constexpr uint32_t PREPARE_MS=3000, CAL_TIMEOUT_MS=3200;

CheezsEMG sEMG(INPUT_PIN,DETECT_PIN,SAMPLE_RATE);
HardwareSerial bluetoothSerial(2);
// 串口1预留：仅初始化，不自动读取、打印或触发业务。
HardwareSerial extraSerial(1);
constexpr uint32_t EXTRA_SERIAL_BAUDRATE=9600;
constexpr uint32_t BRAIN_BAUDRATE=57600;
uint8_t brainSignalQuality=0, brainAttention=0, brainMeditation=0;
bool brainDataReady=false;

struct BrainParser {
  enum class State { SeekFirstAA, SeekSecondAA, ReadLength, ReadPayload, ReadChecksum } state=State::SeekFirstAA;
  uint8_t length=0, index=0, checksum=0, payload[32]={};
  uint16_t sum=0;
  void reset() { state=State::SeekFirstAA; length=index=checksum=0; sum=0; }
  void consume(uint8_t b) {
    switch (state) {
      case State::SeekFirstAA: if (b==0xAA) state=State::SeekSecondAA; break;
      case State::SeekSecondAA: state=(b==0xAA)?State::ReadLength:((b==0xAA)?State::SeekSecondAA:State::SeekFirstAA); break;
      case State::ReadLength:
        length=b;
        if (length==0x20) { index=0; sum=0; state=State::ReadPayload; }
        else reset();
        break;
      case State::ReadPayload:
        payload[index++]=b; sum+=b;
        if (index>=length) state=State::ReadChecksum;
        break;
      case State::ReadChecksum:
        checksum=b;
        if (checksum==uint8_t((~sum)&0xFF)) {
          brainSignalQuality=payload[1];
          brainAttention=payload[29];
          brainMeditation=payload[31];
          brainDataReady=true;
        }
        reset();
        break;
    }
  }
};
BrainParser brainParser;
void readBrainData() {
  while (bluetoothSerial.available()) brainParser.consume(uint8_t(bluetoothSerial.read()));
}

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0,U8X8_PIN_NONE,22,21);
portMUX_TYPE sampleMux=portMUX_INITIALIZER_UNLOCKED;

struct EmgView {
  uint32_t timeUs=0, sequence=0, wearLosses=0, wearPauses=0, faults=0;
  int raw=0, oldEnv=0;
  float newEnv=0;
  bool worn=false, warmed=false, wearUncertain=false;
};
EmgView sharedEmg;
bool samplingReady=false, oledReady=false, mpuReady=false;
uint8_t mpuAddress=0;
uint32_t seenWearLosses=0, seenWearPauses=0, seenFaults=0, seenSequence=0;
uint32_t lastCheckMs=0, lastControlMs=0, lastDrawMs=0, lastTelemetryMs=0;
Vec3 gyroBias={}, gravity={};
bool gravityReady=false, liveValid=false, liveStable=false;
char livePosture=0;
bool livePoseValid=false;

// 以下数据只用于诊断显示，不参与动作判定，也不改变校准阈值。
enum class RegisterResult { Ok, Communication, Incomplete };
struct RegisterTrace {
  RegisterResult result=RegisterResult::Ok;
  uint8_t code=0;
  size_t expected=0, received=0, available=0;
};
RegisterTrace registerTrace;
enum class MpuReadState {
  NotAttempted, NotReady, StatusCommunication, StatusIncomplete,
  NoData, DataCommunication, DataIncomplete, DecodeFailed, Ok
};
struct MpuDiagnostics {
  MpuReadState state=MpuReadState::NotAttempted;
  uint32_t attempts=0, successes=0, noData=0, statusErrors=0, dataErrors=0;
  uint32_t lastSuccessMs=0;
  bool haveSample=false;
  Vec3 acceleration={}, gyro={};
  RegisterTrace transfer;
};
MpuDiagnostics mpuDiagnostics;
struct InputDiagnostics {
  bool mpuOk=false, emgOk=false, changedWear=false, changedFault=false;
  bool newSample=false, fresh=false, contactPaused=false;
  uint32_t ageUs=0;
};
InputDiagnostics inputDiagnostics;
bool calibrationInputContext=false;
Vec3 diagnosticNeutral={};
bool diagnosticNeutralReady=false;
sixops::PoseMap pose;
sixops::Recognizer recognizer;
sixops::Thresholds thresholds={};

// 总线、屏幕、校准和动作识别仅由主循环操作，肌电采样任务不访问它们。
enum class Calibration { NeedNeutral, NeedUp, NeedDown, NeedSide, NeedOtherSide, NeedGrip, Complete };
Calibration calibration=Calibration::NeedNeutral;
bool collecting=false, diagnostics=false;
char captureKind=0;
uint32_t captureStartedMs=0;
size_t captureCount=0, skippedCount=0;
struct CalibrationDiagnostics {
  size_t mpu=0, emg=0, notWorn=0, wearInterrupted=0, contactUnstable=0;
  size_t acceleration=0, motion=0, direction=0, other=0;
  size_t postureChanged=0, notNeutral=0;
};
CalibrationDiagnostics calibrationDiagnostics;
float relaxedRef[CAL_POINTS]={}, gripRef[CAL_POINTS]={};
Vec3 vectorSum={}, gyroSum={};
Vec3 poseSamples[CAL_POINTS]={};
char lastKey=0;
uint32_t eventCount=0;
const char* notice="发送c开始校准";

EmgView emgSnapshot();
void sampleTask(void*);
bool readRegisters(uint8_t,uint8_t,uint8_t*,size_t);
bool writeRegister(uint8_t,uint8_t,uint8_t);
bool initMpu();
bool checkMpuConfig();
bool readMpu(Vec3&,Vec3&);
void beginCalibration(char);
void calibrationStep(uint32_t,const EmgView&,bool,Vec3,Vec3);
void failCalibration(const char*);
void finishCalibration();
void saveCalibration();
void clearSavedCalibration();
void controlStep();
void receiveCommands();
void emitAction(char);
const char* actionName(char);
const char* postureName(char);
char referenceKey(char);
const char* calibrationName();
void drawScreen();
void printStatus();
void printHelp();
const char* mpuReadName();
bool mpuSampleCurrent();
bool naturalAngle(Vec3,float&);
void printMpuDiagnostics(bool);
void printCalibrationDiagnostics();
void printCalibrationReference(Vec3,bool);

EmgView emgSnapshot() {
  portENTER_CRITICAL(&sampleMux);
  EmgView copy=sharedEmg;
  portEXIT_CRITICAL(&sampleMux);
  return copy;
}

void sampleTask(void* parameter) {
  (void)parameter;
  TickType_t wake=xTaskGetTickCount();
  const uint32_t boot=millis();
  uint32_t previousUs=0;
  EmgView value;
  sixops::FloatEnvelope envelope;
  sixops::WearFilter contact;
  for (;;) {
    const uint32_t timeUs=micros();
    sEMG.processSignal();
    value.raw=sEMG.getRawSignal();
    const float filtered=sEMG.getFilteredSignal();
    value.oldEnv=sEMG.getEnvelopeSignal();
    value.newEnv=envelope.add(filtered);
    const bool previousWorn=value.worn, previousUncertain=value.wearUncertain;
    value.worn=contact.update(millis(),sEMG.getDetectSignal()==HIGH);
    value.wearUncertain=contact.uncertain();
    value.warmed=uint32_t(millis()-boot)>=1500;
    if (previousWorn && !value.worn) ++value.wearLosses;
    if (!previousUncertain && value.wearUncertain) ++value.wearPauses;
    const uint32_t interval=timeUs-previousUs;
    if (!isfinite(filtered) || !isfinite(value.newEnv) ||
        value.raw<=3 || value.raw>=1020 ||
        (value.sequence>0 && (interval<1000 || interval>3000))) ++value.faults;
    previousUs=timeUs;
    value.timeUs=timeUs;
    ++value.sequence;
    for (size_t i=0;i<3;++i)
      digitalWrite(LED_PINS[i],value.warmed && value.worn &&
                   value.oldEnv>=LED_THRESHOLDS[i] ? HIGH : LOW);
    portENTER_CRITICAL(&sampleMux);
    sharedEmg=value;
    portEXIT_CRITICAL(&sampleMux);
    vTaskDelayUntil(&wake,SAMPLE_TICKS);
  }
}

bool readRegisters(uint8_t address,uint8_t reg,uint8_t* data,size_t count) {
  registerTrace=RegisterTrace();
  registerTrace.expected=count;
  Wire.beginTransmission(address);
  Wire.write(reg);
  registerTrace.code=Wire.endTransmission(false);
  if (registerTrace.code!=0) {
    registerTrace.result=RegisterResult::Communication;
    return false;
  }
  const size_t received=Wire.requestFrom(address,count,true);
  registerTrace.received=received;
  registerTrace.available=size_t(Wire.available());
  if (received!=count || registerTrace.available<count) {
    registerTrace.result=RegisterResult::Incomplete;
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i=0;i<count;++i) data[i]=uint8_t(Wire.read());
  return true;
}
bool writeRegister(uint8_t address,uint8_t reg,uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission(true)==0;
}
bool checkMpuConfig() {
  uint8_t identity=0, power[2]={}, config[4]={};
  return readRegisters(mpuAddress,0x75,&identity,1) && identity==0x68 &&
         readRegisters(mpuAddress,0x6B,power,2) && power[0]==1 && power[1]==0 &&
         readRegisters(mpuAddress,0x19,config,4) &&
         config[0]==4 && config[1]==3 && config[2]==0 && config[3]==0;
}
bool initMpu() {
  mpuReady=false;
  for (uint8_t address=0x68;address<=0x69;++address) {
    uint8_t identity=0;
    if (readRegisters(address,0x75,&identity,1) && identity==0x68) {
      mpuAddress=address;
      break;
    }
  }
  if (!mpuAddress) return false;
  if (!writeRegister(mpuAddress,0x6B,0x80)) return false;
  delay(100); // 等待姿态传感器完成硬件复位，再继续配置。
  if (!writeRegister(mpuAddress,0x6B,1) || !writeRegister(mpuAddress,0x6C,0) ||
      !writeRegister(mpuAddress,0x19,4) || !writeRegister(mpuAddress,0x1A,3) ||
      !writeRegister(mpuAddress,0x1B,0) || !writeRegister(mpuAddress,0x1C,0) ||
      !writeRegister(mpuAddress,0x38,1)) return false;
  delay(30);
  mpuReady=checkMpuConfig();
  gravityReady=false;
  return mpuReady;
}
bool readMpu(Vec3& acceleration,Vec3& gyro) {
  ++mpuDiagnostics.attempts;
  if (!mpuReady) {
    mpuDiagnostics.state=MpuReadState::NotReady;
    return false;
  }
  uint8_t status=0, bytes[14];
  if (!readRegisters(mpuAddress,0x3A,&status,1)) {
    mpuDiagnostics.transfer=registerTrace;
    mpuDiagnostics.state=registerTrace.result==RegisterResult::Communication ?
        MpuReadState::StatusCommunication : MpuReadState::StatusIncomplete;
    ++mpuDiagnostics.statusErrors;
    return false;
  }
  if (!(status&1)) {
    mpuDiagnostics.state=MpuReadState::NoData;
    ++mpuDiagnostics.noData;
    return false;
  }
  if (!readRegisters(mpuAddress,0x3B,bytes,sizeof(bytes))) {
    mpuDiagnostics.transfer=registerTrace;
    mpuDiagnostics.state=registerTrace.result==RegisterResult::Communication ?
        MpuReadState::DataCommunication : MpuReadState::DataIncomplete;
    ++mpuDiagnostics.dataErrors;
    return false;
  }
  if (!sixops::decodeMpu(bytes,sizeof(bytes),acceleration,gyro)) {
    mpuDiagnostics.state=MpuReadState::DecodeFailed;
    ++mpuDiagnostics.dataErrors;
    return false;
  }
  mpuDiagnostics.state=MpuReadState::Ok;
  ++mpuDiagnostics.successes;
  mpuDiagnostics.haveSample=true;
  mpuDiagnostics.lastSuccessMs=millis();
  mpuDiagnostics.acceleration=acceleration;
  mpuDiagnostics.gyro=gyro;
  return true;
}

const char* mpuReadName() {
  switch (mpuDiagnostics.state) {
    case MpuReadState::NotAttempted: return "尚未读取";
    case MpuReadState::NotReady: return "未初始化";
    case MpuReadState::StatusCommunication: return "状态寄存器通信失败";
    case MpuReadState::StatusIncomplete: return "状态寄存器数据不足（可能通信失败）";
    case MpuReadState::NoData: return "无新数据";
    case MpuReadState::DataCommunication: return "测量寄存器通信失败";
    case MpuReadState::DataIncomplete: return "测量数据不足（可能通信失败）";
    case MpuReadState::DecodeFailed: return "数据解析失败";
    case MpuReadState::Ok: return "正常";
  }
  return "未知";
}
bool mpuSampleCurrent() {
  return mpuReady && mpuDiagnostics.haveSample &&
      mpuDiagnostics.state==MpuReadState::Ok &&
      uint32_t(millis()-mpuDiagnostics.lastSuccessMs)<=120;
}
bool naturalAngle(Vec3 v,float& angle) {
  Vec3 direction;
  if (!diagnosticNeutralReady || !sixops::unit(v,direction)) return false;
  const float cosine=fmaxf(-1.0f,fminf(1.0f,sixops::dot(direction,diagnosticNeutral)));
  angle=acosf(cosine)*57.2957795f;
  return isfinite(angle);
}
void printMpuDiagnostics(bool detailed) {
  const bool current=mpuSampleCurrent();
  Serial.printf("%s初始化=%s，读取=%s，成功次数=%lu，最近成功距今=",
      detailed?"【姿态诊断】":"【实时姿态】",mpuReady?"就绪":"异常",mpuReadName(),
      static_cast<unsigned long>(mpuDiagnostics.successes));
  if (mpuDiagnostics.haveSample)
    Serial.printf("%lu毫秒",static_cast<unsigned long>(millis()-mpuDiagnostics.lastSuccessMs));
  else Serial.print("尚无成功读取");
  if (mpuDiagnostics.haveSample && uint32_t(millis()-mpuDiagnostics.lastSuccessMs)>120)
    Serial.print("，数据已过期");
  if (detailed) {
    Serial.printf("，尝试次数=%lu，无新数据次数=%lu，状态读取失败=%lu，测量读取失败=%lu",
        static_cast<unsigned long>(mpuDiagnostics.attempts),
        static_cast<unsigned long>(mpuDiagnostics.noData),
        static_cast<unsigned long>(mpuDiagnostics.statusErrors),
        static_cast<unsigned long>(mpuDiagnostics.dataErrors));
    if (mpuDiagnostics.state==MpuReadState::StatusCommunication ||
        mpuDiagnostics.state==MpuReadState::StatusIncomplete ||
        mpuDiagnostics.state==MpuReadState::DataCommunication ||
        mpuDiagnostics.state==MpuReadState::DataIncomplete) {
      const RegisterTrace& transfer=mpuDiagnostics.transfer;
      Serial.printf("，寄存器定位返回码=%u，期望字节=%u，实际可用字节=%u，请求返回字节=%u，可读字节=%u",
          unsigned(transfer.code),unsigned(transfer.expected),
          unsigned(std::min(transfer.received,transfer.available)),
          unsigned(transfer.received),unsigned(transfer.available));
    }
  }
  Serial.print("，自然位夹角=");
  float angle=0;
  if (!diagnosticNeutralReady) Serial.print("不可用（自然位未校准）");
  else if (!current || !naturalAngle(mpuDiagnostics.acceleration,angle)) Serial.print("不可用");
  else Serial.printf("%.1f度",angle);
  if (detailed) Serial.print("\n【姿态数值】");
  else Serial.print("，");
  if (!mpuDiagnostics.haveSample) {
    Serial.println("尚无成功样本，加速度和陀螺仪=不可用");
    return;
  }
  const Vec3 acc=mpuDiagnostics.acceleration, gyro=mpuDiagnostics.gyro;
  Serial.printf("样本=%s，加速度(g)=(%.3f,%.3f,%.3f)，陀螺仪(度/秒)=(%.2f,%.2f,%.2f)",
      current?"本次成功读取":"历史成功样本（非当前有效数据）",
      acc.x,acc.y,acc.z,gyro.x,gyro.y,gyro.z);
  if (detailed) {
    const Vec3 rate={gyro.x-gyroBias.x,gyro.y-gyroBias.y,gyro.z-gyroBias.z};
    Serial.printf("，加速度模长=%.3fg，扣零偏转速=%.2f度/秒",
        sqrtf(sixops::dot(acc,acc)),sqrtf(sixops::dot(rate,rate)));
  }
  Serial.println();
}
void printCalibrationDiagnostics() {
  const CalibrationDiagnostics& d=calibrationDiagnostics;
  Serial.printf("【校准诊断】步骤%c：收集=%u，拒绝=%u，姿态读取失败=%u，肌电数据异常=%u，未佩戴=%u，佩戴中断=%u，接触不稳=%u，加速度异常=%u，转动过快=%u，方向无效=%u，其他输入异常=%u，姿势变化=%u，不在自然位=%u。\n",
      captureKind?captureKind:'-',unsigned(captureCount),unsigned(skippedCount),
      unsigned(d.mpu),unsigned(d.emg),unsigned(d.notWorn),unsigned(d.wearInterrupted),
      unsigned(d.contactUnstable),unsigned(d.acceleration),unsigned(d.motion),unsigned(d.direction),unsigned(d.other),
      unsigned(d.postureChanged),unsigned(d.notNeutral));
}
void printCalibrationReference(Vec3 average,bool ok) {
  float angle=0;
  Serial.printf("【校准参考】步骤%c：结果=%s，平均自然位夹角=",captureKind,ok?"通过":"不通过");
  if (naturalAngle(average,angle)) Serial.printf("%.1f度",angle);
  else Serial.print("未校准或不可用");
  const char key=referenceKey(captureKind);
  if (key) {
    if (ok) {
      Serial.printf("，参考姿势=%s，采集波动=%.2f度",postureName(key),pose.spreadDegrees(key));
      const float radius=pose.radiusDegrees(key);
      if (isfinite(radius)) Serial.printf("，当前匹配范围=%.2f度",radius);
      else Serial.print("，匹配范围待其他姿势记录后确定");
    } else {
      Serial.printf("，原因=%s",pose.lastError());
      if (pose.conflictKey()) Serial.printf("，冲突姿势=%s",postureName(pose.conflictKey()));
    }
    Serial.print("；按实际姿势学习，不要求固定角度，范围会随后续参考更新");
  }
  Serial.println();
}
void failCalibration(const char* why) {
  collecting=false;
  notice=why;
  recognizer.reset();
  Serial.printf("【校准失败】步骤%c：%s；请重发当前步骤，或发送c重新开始。\n",captureKind,why);
  printCalibrationDiagnostics();
  printMpuDiagnostics(true);
}
void beginCalibration(char kind) {
  if (collecting && kind!='c') {
    Serial.println("【提示】正在校准，请等待；发送c可重新开始。");
    return;
  }
  if (kind=='c') {
    collecting=false;
    recognizer=sixops::Recognizer();
    pose=sixops::PoseMap();
    calibration=Calibration::NeedNeutral;
    gyroBias={}; gravityReady=false;
    diagnosticNeutralReady=false;
    livePoseValid=false; livePosture=0;
    if (!mpuReady && !initMpu()) {
      notice="姿态异常请检查接线";
      Serial.println("【错误】姿态传感器未就绪，暂不识别操作；请检查接线。");
      return;
    }
  }
  const bool allowed=(kind=='c' && calibration==Calibration::NeedNeutral) ||
      (kind=='u' && calibration==Calibration::NeedUp) ||
      (kind=='d' && calibration==Calibration::NeedDown) ||
      (kind=='g' && calibration==Calibration::NeedGrip);
  if (!allowed) { Serial.println("【错误】请按 c → u → d → r → l → g 的顺序校准。"); return; }
  const EmgView value=emgSnapshot();
  if (!samplingReady || !value.warmed || !mpuReady) {
    Serial.println("【错误】请等待预热完成或检查传感器，再发送校准命令。"); return;
  }
  captureKind=kind;
  captureCount=skippedCount=0;
  calibrationDiagnostics=CalibrationDiagnostics();
  vectorSum={}; gyroSum={};
  captureStartedMs=millis();
  collecting=true;
  recognizer.reset();
  if (kind=='c') notice="自然位置放松";
  else if (kind=='u') notice="按习惯上翘并保持";
  else if (kind=='d') notice="按习惯下压并保持";
  else if (kind=='r') notice="分隔符侧翻并保持";
  else if (kind=='l') notice="另一侧翻并保持";
  else notice="自然位置握拳";
  Serial.printf("【校准准备】步骤%c：%s；3秒后开始，请保持约2秒。\n",kind,notice);
}

void calibrationStep(uint32_t now,const EmgView& emg,bool valid,Vec3 acc,Vec3 gyro) {
  const uint32_t elapsed=now-captureStartedMs;
  if (elapsed<PREPARE_MS) return;
  if (elapsed>=PREPARE_MS+CAL_TIMEOUT_MS) { failCalibration("稳定数据不足"); return; }
  Vec3 direction;
  const float norm=sqrtf(sixops::dot(acc,acc));
  const Vec3 corrected={gyro.x-gyroBias.x,gyro.y-gyroBias.y,gyro.z-gyroBias.z};
  const float speed=sqrtf(sixops::dot(corrected,corrected));
  const bool badEmg=!isfinite(emg.newEnv) || emg.newEnv<0;
  if (!valid || badEmg || !emg.worn || !isfinite(norm) || norm<0.85f || norm>1.15f ||
      !isfinite(speed) || speed>(captureKind=='c'?12.0f:8.0f) ||
      !sixops::unit(acc,direction)) {
    // 一点可能同时被多个条件拒绝；不把读取失败留下的零向量算成加速度异常。
    CalibrationDiagnostics& d=calibrationDiagnostics;
    if (!valid) {
      if (!calibrationInputContext) ++d.other;
      else {
        if (!inputDiagnostics.mpuOk) ++d.mpu;
        if (!inputDiagnostics.emgOk || badEmg) ++d.emg;
        if (inputDiagnostics.changedWear) ++d.wearInterrupted;
        if (inputDiagnostics.contactPaused) ++d.contactUnstable;
        if (inputDiagnostics.mpuOk && inputDiagnostics.emgOk &&
            !inputDiagnostics.changedWear && !inputDiagnostics.contactPaused) ++d.other;
      }
    }
    if (badEmg && (valid || !calibrationInputContext)) ++d.emg;
    if (!emg.worn) ++d.notWorn;
    if (valid || (calibrationInputContext && inputDiagnostics.mpuOk)) {
      if (!isfinite(norm) || norm<0.85f || norm>1.15f) ++d.acceleration;
      if (!isfinite(speed) || speed>(captureKind=='c'?12.0f:8.0f)) ++d.motion;
      Vec3 checked;
      if (!sixops::unit(acc,checked)) ++d.direction;
    }
    if (++skippedCount>15) failCalibration("停稳并检查佩戴");
    return;
  }
  if (captureKind=='g' && pose.classify(direction)!='n') {
    ++calibrationDiagnostics.notNeutral;
    failCalibration("请先回到已记录的自然位置"); return;
  }
  // 记录整段实际波动，再与各姿势的间隔比较，不用固定首点偏移角淘汰样本。
  poseSamples[captureCount]=direction;
  vectorSum.x+=direction.x; vectorSum.y+=direction.y; vectorSum.z+=direction.z;
  gyroSum.x+=gyro.x; gyroSum.y+=gyro.y; gyroSum.z+=gyro.z;
  if (captureKind=='c') relaxedRef[captureCount]=emg.newEnv;
  if (captureKind=='g') gripRef[captureCount]=emg.newEnv;
  ++captureCount;
  if (captureCount==CAL_POINTS) finishCalibration();
}
void finishCalibration() {
  const Vec3 average={vectorSum.x/CAL_POINTS,vectorSum.y/CAL_POINTS,vectorSum.z/CAL_POINTS};
  bool ok=false;
  const char key=referenceKey(captureKind);
  if (key) {
    ok=pose.setReference(key,poseSamples,CAL_POINTS);
    if (ok) {
      if (captureKind=='c') {
        gyroBias={gyroSum.x/CAL_POINTS,gyroSum.y/CAL_POINTS,gyroSum.z/CAL_POINTS};
        diagnosticNeutralReady=sixops::unit(average,diagnosticNeutral);
        calibration=Calibration::NeedUp; notice="放松后发送u";
      } else if (captureKind=='u') {
        calibration=Calibration::NeedDown; notice="放松后发送d";
      } else if (captureKind=='d') {
        calibration=Calibration::NeedSide; notice="放松后发送r";
      } else if (captureKind=='r') {
        calibration=Calibration::NeedOtherSide; notice="放松后发送l";
      } else {
        calibration=Calibration::NeedGrip; notice="放松后发送g";
      }
    }
  }
  if (captureKind=='g') {
    ok=pose.ready() && sixops::gripThresholds(relaxedRef,gripRef,CAL_POINTS,thresholds) &&
       recognizer.configure(thresholds);
    if (ok) { calibration=Calibration::Complete; notice="回到自然位置放松"; saveCalibration(); }
  }
  printCalibrationReference(average,ok);
  if (!ok) {
    if (captureKind=='g') failCalibration("握拳放松差异不足");
    else {
      failCalibration(pose.lastError());
      if (pose.conflictKey())
        Serial.printf("【提示】这次姿势与%s难以区分，请选用能重复且不同的姿势，再发送%c；不要求达到指定角度。\n",
            postureName(pose.conflictKey()),captureKind);
    }
    return;
  }
  collecting=false;
  gravityReady=false;
  Serial.printf("【校准成功】步骤%c：%s。\n",captureKind,notice);
  printCalibrationDiagnostics();
  if (calibration==Calibration::Complete) {
    Serial.printf("【肌电阈值】握拳=%.3f，松开=%.3f。\n",thresholds.on,thresholds.off);
    Serial.println("【提示】校准完成，回到自然位置并放松后可选字；长握联想，候选页短握确认后播报。");
  }

  
}

const char* actionName(char key) {
  return key=='w'?"上翻":key=='s'?"下翻":key=='f'?"分隔符":
         key=='b'?"退格":key=='x'?"联想":key==' '?"确定":"无";
}
const char* postureName(char key) {
  return key=='n'?"自然位":key=='w'?"上翘":key=='s'?"下压":
         key=='f'?"分隔符侧翻":key=='b'?"返回侧翻":"未匹配";
}
char referenceKey(char step) {
  return step=='c'?'n':step=='u'?'w':step=='d'?'s':step=='r'?'f':step=='l'?'b':0;
}
const char* calibrationName() {
  switch (calibration) {
    case Calibration::NeedNeutral: return "等待自然位校准";
    case Calibration::NeedUp: return "等待上翘校准";
    case Calibration::NeedDown: return "等待下压校准";
    case Calibration::NeedSide: return "等待分隔符侧翻校准";
    case Calibration::NeedOtherSide: return "等待返回侧翻校准";
    case Calibration::NeedGrip: return "等待握拳校准";
    case Calibration::Complete: return "校准完成";
  }
  return "未知状态";
}
void emitAction(char key) {
  lastKey=key;
  ++eventCount;
  Serial.printf("【操作】次数=%lu，时间=%lu毫秒，按键=%s，动作=%s。\n",
      static_cast<unsigned long>(eventCount),static_cast<unsigned long>(millis()),
      key==' ' ? "空格" : key=='w'?"w":key=='s'?"s":key=='f'?"f":key=='b'?"b":"x",actionName(key));
  ::dispatchAction(key,true);
}
void controlStep() {
  const uint32_t now=millis();
  const EmgView emg=emgSnapshot();
  const bool changedWear=emg.wearLosses!=seenWearLosses;
  const bool contactPaused=emg.wearUncertain || emg.wearPauses!=seenWearPauses;
  const bool changedFault=emg.faults!=seenFaults;
  const bool newSample=emg.sequence!=seenSequence;
  seenWearLosses=emg.wearLosses; seenWearPauses=emg.wearPauses;
  seenFaults=emg.faults; seenSequence=emg.sequence;
  Vec3 acc={}, gyro={};
  const bool mpuOk=readMpu(acc,gyro);
  const uint32_t emgAgeUs=micros()-emg.timeUs;
  const bool emgOk=samplingReady && emg.warmed && newSample &&
      emgAgeUs<20000 && !changedFault && isfinite(emg.newEnv) && emg.newEnv>=0;
  inputDiagnostics.mpuOk=mpuOk;
  inputDiagnostics.emgOk=emgOk;
  inputDiagnostics.changedWear=changedWear;
  inputDiagnostics.contactPaused=contactPaused;
  inputDiagnostics.changedFault=changedFault;
  inputDiagnostics.newSample=newSample;
  inputDiagnostics.fresh=emgAgeUs<20000;
  inputDiagnostics.ageUs=emgAgeUs;
  const bool sensorsValid=mpuOk && emgOk && !changedWear;
  liveValid=sensorsValid && !contactPaused;
  livePoseValid=false; livePosture=0;
  if (collecting) {
    calibrationInputContext=true;
    calibrationStep(now,emg,liveValid,acc,gyro);
    calibrationInputContext=false;
    return;
  }
  Vec3 checked;
  if (sensorsValid && emg.worn && contactPaused && sixops::unit(acc,checked)) {
    gravityReady=false;
    liveStable=false;
    recognizer.pauseContact(now);
    return;
  }
  const float norm=sqrtf(sixops::dot(acc,acc));
  const Vec3 rate={gyro.x-gyroBias.x,gyro.y-gyroBias.y,gyro.z-gyroBias.z};
  liveStable=mpuOk && norm>=0.85f && norm<=1.15f && sqrtf(sixops::dot(rate,rate))<25;
  if (!mpuOk) gravityReady=false;
  else if (!gravityReady || !liveStable) { gravity=acc; gravityReady=true; }
  else {
    gravity.x+=0.30f*(acc.x-gravity.x);
    gravity.y+=0.30f*(acc.y-gravity.y);
    gravity.z+=0.30f*(acc.z-gravity.z);
  }
  livePoseValid=calibration==Calibration::Complete && pose.ready() && mpuOk &&
      gravityReady && sixops::unit(gravity,checked);
  if (livePoseValid) livePosture=pose.classify(gravity);
  const char event=recognizer.update({now,emg.newEnv,livePosture,
      liveValid && livePoseValid,emg.worn && !changedWear,liveStable});
  if (event) emitAction(event);
}

void printHelp() {
  Serial.println("\n【帮助】手势选字与语音播报；原main和六动作测试工程保持不变。");
  Serial.println("【帮助】校准按 c/u/d/r/l/g 顺序，每次3秒准备加约2秒保持；失败重发当前步骤。");
  Serial.println("【帮助】c 自然位放松；u 上翘；d 下压；r 分隔符侧翻；l 返回侧翻；g 自然位握拳。");
  Serial.println("【帮助】用自己能舒适重复的姿势，不要求指定角度；四个方向独立学习，不用镜像推断。");
  Serial.println("【帮助】校准后：上翘w，下压s，校准侧翻f，反向侧翻b，短握松开为空格确定，长握1.5秒为x联想。");
  Serial.println("【帮助】每次回自然位并放松；握拳时保持自然位。短握不会在刚握住时确认。");
  Serial.println("【帮助】抬腕取消握拳后，停稳仍可选方向；短暂接触不稳先暂停，确认脱落后需回位放松。");
  Serial.println("【帮助】p 完整诊断；t 每秒一次精简姿态诊断开关；h 帮助；c 重新校准。断电不保存校准。");
  Serial.println("【帮助】姿态就绪仅表示初始化通过；请看读取结果、成功次数和最近成功距今判断实时数据。");
  Serial.println("【帮助】自然位总倾角仅供诊断；姿势匹配按实测参考判断，未完成学习显示未校准，分不开时显示未匹配。");
}
void printStatus() {
  const EmgView emg=emgSnapshot();
  Serial.printf("【状态】姿态=%s，屏幕=%s，采样=%s，预热=%s，佩戴=%s，校准=%s，采集=%s，原始=%d，旧包络=%d，新包络=%.3f，",
      mpuReady?"就绪":"异常",oledReady?"就绪":"未连接",samplingReady?"运行":"失败",
      emg.warmed?"完成":"等待",emg.wearUncertain?"接触不稳":emg.worn?"已佩戴":"未佩戴",calibrationName(),collecting?"进行中":"未采集",
      emg.raw,emg.oldEnv,emg.newEnv);
  Serial.print("姿势匹配=");
  if (calibration!=Calibration::Complete) Serial.print("未校准");
  else if (!livePoseValid || !mpuSampleCurrent()) Serial.print("不可用");
  else Serial.print(postureName(livePosture));
  Serial.printf("，识别=%s，操作次数=%lu\n",recognizer.stateName(),
      static_cast<unsigned long>(eventCount));
  printMpuDiagnostics(true);
  Serial.printf("【输入诊断】最近控制周期：肌电=%s，新采样=%s，采样距今=%lu微秒，采样异常=%s，佩戴中断=%s，接触暂停=%s；累计采样异常=%lu，确认脱落次数=%lu，接触暂停次数=%lu。\n",
      inputDiagnostics.emgOk?"有效":"无效",inputDiagnostics.newSample?"有":"无",
      static_cast<unsigned long>(inputDiagnostics.ageUs),inputDiagnostics.changedFault?"有":"无",
      inputDiagnostics.changedWear?"有":"无",inputDiagnostics.contactPaused?"有":"无",
      static_cast<unsigned long>(emg.faults),static_cast<unsigned long>(emg.wearLosses),
      static_cast<unsigned long>(emg.wearPauses));
  if (captureKind) printCalibrationDiagnostics();
}
void receiveCommands() {
  unsigned budget=32;
  while (budget-- && Serial.available()) {
    char c=char(Serial.read());
    if (c>='A' && c<='Z') c=char(c-'A'+'a');
    if (::handleApplicationCommand(c)) continue;
    if (c=='c' || c=='u' || c=='d' || c=='r' || c=='l' || c=='g') beginCalibration(c);
    else if (c=='h' || c=='?') printHelp();
    else if (c=='p') printStatus();
    else if (c=='t') { diagnostics=!diagnostics; Serial.printf("【提示】诊断输出已%s。\n",diagnostics?"开启":"关闭"); }
    else if (c!='\r' && c!='\n') Serial.println("【错误】校准c/u/d/r/l/g，诊断h/p/t；校准回位后可用w/s/空格/f/x/b。");
  }
}
// 区分等待动作与传感器故障，避免把移动、未回位都提示成电极脱落。
const char* inputHint(const EmgView& emg) {
  if (!samplingReady) return "采样任务失败";
  if (!emg.worn) return "未佩戴请检查电极";
  if (emg.wearUncertain || emg.wearPauses!=seenWearPauses) return "接触不稳暂停操作";
  if (!emg.warmed) return "正在预热";
  if (emg.faults!=seenFaults || inputDiagnostics.changedFault ||
      !isfinite(emg.newEnv) || emg.newEnv<0) return "肌电数据异常";
  if (uint32_t(micros()-emg.timeUs)>=20000) return "肌电采样已过期";
  if (!mpuReady || !mpuSampleCurrent()) return "姿态数据异常";
  if (emg.wearLosses!=seenWearLosses || inputDiagnostics.changedWear)
    return "佩戴恢复请回位";
  if (!liveValid) return "等待有效采样";
  if (!livePoseValid) return "姿势暂不可用";
  if (!liveStable) return "正在移动请停稳";
  if (recognizer.needsRearm(livePosture)) {
    if (livePosture!='n') return "请回到自然位置";
    if (emg.newEnv>thresholds.off) return "请松开握拳";
    return "放松保持片刻";
  }
  if (!livePosture) return "姿势未匹配";
  return recognizer.stateName();
}
void drawScreen() {
  if (!oledReady) return;
  const EmgView emg=emgSnapshot();
  display.clearBuffer();
  display.setFont(u8g2_font_wqy12_t_gb2312);
  display.setFontPosTop();
  // 中文字高12像素，按13像素行距显示五行，避免上下覆盖。
  display.setCursor(0,0); display.print("六操作 次数:");
  if (eventCount<=9999) display.print(eventCount);
  else display.print("9999+");
  display.setCursor(0,13);
  if (!samplingReady) display.print("采样任务失败");
  else if (!mpuReady) display.print("姿态异常 发送c");
  else if (!emg.warmed) display.print("正在预热");
  else if (collecting) {
    const uint32_t elapsed=millis()-captureStartedMs;
    if (elapsed<PREPARE_MS) { display.print("准备 "); display.print((PREPARE_MS-elapsed+999)/1000); display.print("秒"); }
    else { display.print("保持 "); display.print(captureCount); display.print("/100"); }
  } else if (calibration!=Calibration::Complete) display.print("请先完成校准");
  else display.print(inputHint(emg));
  display.setCursor(0,26);
  if (calibration!=Calibration::Complete || collecting) display.print(notice);
  else { display.print("新:"); display.print(emg.newEnv,2); display.print(" 旧:"); display.print(emg.oldEnv); }
  display.setCursor(0,39);
  if (calibration==Calibration::Complete) {
    // 握拳时显示计时；其余时间显示匹配到的实际姿势，不再套用固定上下轴。
    if (recognizer.holdMs()>0) {
      display.print("握住 "); display.print(recognizer.holdMs()/1000.0f,1); display.print("/1.5秒");
    } else if (!livePoseValid || !mpuSampleCurrent()) {
      display.print("姿势暂不可用");
    } else {
      display.print("姿势:"); display.print(postureName(livePosture));
    }
  } else display.print("c u d r l g");
  display.setCursor(0,52);
  display.print("最近:"); display.print(actionName(lastKey));
  if (lastKey && lastKey!=' ') { display.print('('); display.print(lastKey); display.print(')'); }
  display.sendBuffer();
}

void drawConfigWaitScreen() {
  if (!oledReady) return;
  display.clearBuffer();
  display.setFont(u8g2_font_wqy12_t_gb2312);
  display.setFontPosTop();
  display.drawUTF8(0,0,"等待Wi-Fi配置");
  display.drawUTF8(0,16,"热点: ESP32");
  display.drawUTF8(0,32,"手机连热点后打开:");
  display.drawUTF8(0,48,"192.168.4.1");
  display.sendBuffer();
}

bool loadSavedCalibration() {
  Preferences store;
  if (!store.begin("gesture-cal",true)) return false;
  if (!store.getBool("valid",false)) { store.end(); return false; }
  float data[20];
  for (size_t i=0;i<20;++i) data[i]=store.getFloat((String("p")+String(static_cast<unsigned>(i))).c_str(),NAN);
  sixops::Thresholds restored={store.getFloat("on",NAN),store.getFloat("off",NAN)};
  store.end();
  if (!pose.importData(data,20) || !recognizer.configure(restored)) return false;
  thresholds=restored;
  calibration=Calibration::Complete;
  notice="已加载保存的手势校准";
  return true;
}
void saveCalibration() {
  float data[20];
  if (!pose.exportData(data,20)) return;
  Preferences store;
  if (!store.begin("gesture-cal",false)) return;
  for (size_t i=0;i<20;++i) store.putFloat((String("p")+String(static_cast<unsigned>(i))).c_str(),data[i]);
  store.putFloat("on",thresholds.on);
  store.putFloat("off",thresholds.off);
  store.putBool("valid",true);
  store.end();
  Serial.println("【校准】手势校准已保存，断电后仍可使用。");
}
void clearSavedCalibration() {
  Preferences store;
  if (store.begin("gesture-cal",false)) { store.clear(); store.end(); }
}

void setup() {
  Serial.begin(115200);
  bluetoothSerial.begin(57600,SERIAL_8N1,BLUETOOTH_RX_PIN,BLUETOOTH_TX_PIN);
  extraSerial.begin(EXTRA_SERIAL_BAUDRATE,SERIAL_8N1,EXTRA_SERIAL_RX_PIN,EXTRA_SERIAL_TX_PIN);
  for (uint8_t pin:LED_PINS) { digitalWrite(pin,LOW); pinMode(pin,OUTPUT); }
  pinMode(INPUT_PIN,INPUT);
  analogReadResolution(10);
  analogSetPinAttenuation(INPUT_PIN,ADC_11db);
  sEMG.begin();
  Wire.begin(21,22);
  Wire.setClock(400000);
  Wire.setTimeOut(10);
  for (uint8_t address=0x3C;address<=0x3D;++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission()==0) {
      display.setBusClock(400000);
      display.setI2CAddress(address<<1);
      oledReady=display.begin();
      break;
    }
  }
  if (oledReady) {
    display.enableUTF8Print();
    display.setFont(u8g2_font_wqy12_t_gb2312);
    display.setFontPosTop();
  }
  initMpu();
  samplingReady=xTaskCreatePinnedToCore(sampleTask,"EMG500Hz",4096,nullptr,2,nullptr,1)==pdPASS;
  loadSavedCalibration();
  Serial.println("【启动】手势语音识别版1.1；如未找到已保存校准，请完成六步校准。");
  if (!samplingReady) Serial.println("【错误】肌电采样任务创建失败。");
  if (!mpuReady) Serial.println("【错误】未找到姿态传感器或配置异常，请检查接线后发送c。");
  if (!oledReady) Serial.println("【提示】显示屏未连接，仍可通过串口进行测试。");
  printHelp();
  drawScreen();
}
void loop() {
  readBrainData();
  uint32_t now=millis();
  if (mpuReady && uint32_t(now-lastCheckMs)>=1000) {
    lastCheckMs=now;
    if (!checkMpuConfig()) {
      mpuReady=false; gravityReady=false; recognizer.reset();
      if (collecting) failCalibration("姿态读取失败");
      notice="姿态异常 发送c";
      Serial.println("【错误】姿态传感器配置丢失或读取失败，已暂停识别；发送c重新校准。");
    }
  }
  now=millis();
  if (uint32_t(now-lastControlMs)>=CONTROL_MS) {
    lastControlMs=now;
    controlStep();
  }
  if (diagnostics && uint32_t(now-lastTelemetryMs)>=1000) {
    lastTelemetryMs=now;
    printMpuDiagnostics(false);
  }
}

} // namespace gesture
