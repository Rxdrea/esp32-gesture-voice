#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <algorithm>

namespace sixops {
struct Vec3 { float x, y, z; };
struct Thresholds { float on, off; };
inline int32_t signedWord(const uint8_t* p) {
  const uint32_t u = (uint32_t(p[0]) << 8) | p[1];
  return u >= 32768 ? int32_t(u)-65536 : int32_t(u);
}
inline bool decodeMpu(const uint8_t* data, size_t size, Vec3& accel, Vec3& gyro) {
  if (!data || size != 14) return false;
  accel = {signedWord(data)/16384.0f, signedWord(data+2)/16384.0f,
           signedWord(data+4)/16384.0f};
  gyro = {signedWord(data+8)/131.0f, signedWord(data+10)/131.0f,
          signedWord(data+12)/131.0f};
  return true;
}
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline bool unit(Vec3 v, Vec3& out) {
  const float n = sqrtf(dot(v,v));
  if (!isfinite(n) || n < 0.00001f) return false;
  out = {v.x/n, v.y/n, v.z/n};
  return true;
}
inline Vec3 subtract(Vec3 a, Vec3 b, float scale) {
  return {a.x-b.x*scale, a.y-b.y*scale, a.z-b.z*scale};
}

// 沿用波形采集版的32点浮点包络；该读数不是握力百分比。
class FloatEnvelope {
 public:
  float add(float filtered) {
    sum_ -= history_[index_];
    history_[index_] = fabsf(filtered);
    sum_ += history_[index_];
    index_ = (index_+1)%32;
    return static_cast<float>(2.0*fmax(0.0,sum_)/32.0);
  }
 private:
  float history_[32] = {};
  double sum_ = 0;
  unsigned index_ = 0;
};

// 单位向量间夹角使用叉积，避免小角度时 acos 的精度损失；不限制大于90度的姿势。
inline float angleBetweenUnits(Vec3 a,Vec3 b) {
  const Vec3 cross={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
  return atan2f(sqrtf(dot(cross,cross)),dot(a,b))*57.2957795f;
}

// 自然位和四个动作分别学习，不假定轴向、固定倾角、正交关系或镜像对称。
// 模板间留出未知区域；校准波动必须连同余量一起落在各自区域内。
class PoseMap {
 public:
  bool setReference(char key,const Vec3* samples,size_t count) {
    error_=""; conflict_=0;
    const int index=indexFor(key);
    if (index<0) { error_="未知姿势标签"; return false; }
    for (int i=index;i<5;++i) references_[i]=Reference();
    for (int i=0;i<index;++i) {
      if (!references_[i].present) { error_="请先完成前面的姿势学习"; return false; }
    }
    if (!samples || count<50 || count>250) { error_="有效姿势样本不足"; return false; }
    Vec3 sum={};
    for (size_t i=0;i<count;++i) {
      Vec3 g;
      if (!unit(samples[i],g)) { error_="姿势样本无效"; return false; }
      sum.x+=g.x; sum.y+=g.y; sum.z+=g.z;
    }
    Reference candidate;
    const Vec3 average={sum.x/count,sum.y/count,sum.z/count};
    if (!unit(average,candidate.center)) { error_="姿势变化过大，无法得到参考"; return false; }
    for (size_t i=0;i<count;++i) {
      Vec3 g;
      if (!unit(samples[i],g)) return false;
      candidate.spread=fmaxf(candidate.spread,angleBetweenUnits(g,candidate.center));
    }
    for (int i=0;i<index;++i) {
      const float separation=angleBetweenUnits(candidate.center,references_[i].center);
      // 0.0001度仅处理浮点数重合；不是人体动作的固定最低角度。
      // 各区域取最近参考距离的40%，采集波动再留25%余量。
      if (separation<=0.0001f || candidate.spread*1.25f>=separation*0.40f ||
          references_[i].spread*1.25f>=separation*0.40f) {
        error_="姿势与已记录动作过近或波动重叠";
        conflict_=keyFor(i);
        return false;
      }
    }
    candidate.present=true;
    references_[index]=candidate;
    return true;
  }
  char classify(Vec3 v) const {
    Vec3 g;
    if (!ready() || !unit(v,g)) return 0;
    char result=0;
    for (int i=0;i<5;++i) {
      const float distance=angleBetweenUnits(g,references_[i].center);
      if (distance<=radiusDegrees(keyFor(i))) {
        if (result) return 0;
        result=keyFor(i);
      }
    }
    return result;
  }
  bool ready() const {
    for (const Reference& r:references_) if (!r.present) return false;
    return true;
  }
  bool hasReference(char key) const {
    const int i=indexFor(key);
    return i>=0 && references_[i].present;
  }
  float spreadDegrees(char key) const {
    const int i=indexFor(key);
    return i>=0 && references_[i].present ? references_[i].spread : NAN;
  }
  float radiusDegrees(char key) const {
    const int i=indexFor(key);
    if (i<0 || !references_[i].present) return NAN;
    float nearest=180.0f;
    bool haveOther=false;
    for (int j=0;j<5;++j) {
      if (i==j || !references_[j].present) continue;
      nearest=fminf(nearest,angleBetweenUnits(references_[i].center,references_[j].center));
      haveOther=true;
    }
    // 默认留出动作间距的20%作为重复动作余量，再加入实测波动的125%。
    // 最多使用间距的40%，两个模板之间始终保留不能强行归类的区域。
    return haveOther ? fminf(nearest*0.40f,nearest*0.20f+references_[i].spread*1.25f) : NAN;
  }
  const char* lastError() const { return error_; }
  char conflictKey() const { return conflict_; }
  bool exportData(float* out,size_t count) const {
    if (!out || count<20 || !ready()) return false;
    for (int i=0;i<5;++i) {
      out[i*4]=references_[i].center.x;
      out[i*4+1]=references_[i].center.y;
      out[i*4+2]=references_[i].center.z;
      out[i*4+3]=references_[i].spread;
    }
    return true;
  }
  bool importData(const float* in,size_t count) {
    if (!in || count<20) return false;
    Reference restored[5];
    for (int i=0;i<5;++i) {
      Vec3 center={in[i*4],in[i*4+1],in[i*4+2]};
      if (!isfinite(center.x)||!isfinite(center.y)||!isfinite(center.z) ||
          !isfinite(in[i*4+3]) || in[i*4+3]<0 || !unit(center,restored[i].center)) return false;
      restored[i].spread=in[i*4+3];
      restored[i].present=true;
    }
    for (int i=0;i<5;++i) references_[i]=restored[i];
    error_=""; conflict_=0;
    return true;
  }
 private:
  struct Reference {
    Vec3 center={};
    float spread=0;
    bool present=false;
  };
  Reference references_[5];
  const char* error_="";
  char conflict_=0;
  static int indexFor(char key) {
    return key=='n'?0:key=='w'?1:key=='s'?2:key=='f'?3:key=='b'?4:-1;
  }
  static char keyFor(int i) { return "nwsfb"[i]; }
};

inline bool gripThresholds(const float* relaxed, const float* grip,
                           size_t count, Thresholds& out) {
  if (!relaxed || !grip || count < 50 || count > 250) return false;
  float r[250], g[250];
  for (size_t i=0; i<count; ++i) {
    if (!isfinite(relaxed[i]) || !isfinite(grip[i]) || relaxed[i]<0 || grip[i]<0)
      return false;
    r[i]=relaxed[i]; g[i]=grip[i];
  }
  std::sort(r,r+count); std::sort(g,g+count);
  const float relaxedHigh = r[(count-1)*95/100];
  const float gripLow = g[(count-1)*20/100];
  const float gap = gripLow-relaxedHigh;
  if (gap < fmaxf(1.0f,relaxedHigh*0.4f)) return false;
  out = {relaxedHigh+gap*0.60f, relaxedHigh+gap*0.30f};
  return true;
}

// 接触信号一变低就暂停输入；短暂抖动不立刻清掉已取得的方向操作资格。
// 连续低100毫秒或反复不稳200毫秒确认脱落；重新稳定为高40毫秒才恢复。
class WearFilter {
 public:
  bool update(uint32_t now,bool high) {
    if (!worn_) {
      uncertain_=false;
      lowPending_=false;
      if (!high) highPending_=false;
      else {
        if (!highPending_) { highAt_=now; highPending_=true; }
        if (uint32_t(now-highAt_)>=40) {
          worn_=true; highPending_=false;
        }
      }
      return worn_;
    }
    if (!high) {
      if (!uncertain_) { uncertain_=true; uncertainAt_=now; }
      highPending_=false;
      if (!lowPending_) { lowPending_=true; lowAt_=now; }
    } else {
      lowPending_=false;
      if (!uncertain_) return true;
      if (!highPending_) { highPending_=true; highAt_=now; }
      if (uint32_t(now-highAt_)>=40) {
        uncertain_=false; highPending_=false;
        return true;
      }
    }
    if ((lowPending_ && uint32_t(now-lowAt_)>=100) ||
        (uncertain_ && uint32_t(now-uncertainAt_)>=200)) {
      worn_=uncertain_=lowPending_=highPending_=false;
    }
    return worn_;
  }
  bool uncertain() const { return uncertain_; }
 private:
  bool worn_=false,uncertain_=false,highPending_=false,lowPending_=false;
  uint32_t uncertainAt_=0,highAt_=0,lowAt_=0;
};

struct Input {
  uint32_t ms;
  float envelope;
  char posture;
  bool valid, worn, stable;
};
class Recognizer {
 public:
  static constexpr uint32_t LONG_MS = 1500;
  static constexpr uint32_t RELEASE_MS = 400;
  static constexpr uint32_t REARM_MS = 300;
  static constexpr uint32_t DIRECTION_MS = 180;
  static constexpr uint32_t ONSET_MS = 120;

  bool configure(Thresholds t) {
    enabled_ = isfinite(t.on) && isfinite(t.off) && t.on>t.off && t.off>=0;
    thresholds_ = t;
    reset();
    return enabled_;
  }
  void reset() {
    state_ = Locked;
    haveLast_ = neutralPending_ = onsetPending_ = releasePending_ = false;
    direction_=0;
  }
  // 不把接触不确定期间的时间累计进握拳、回位或方向停稳计时。
  void pauseContact(uint32_t now) {
    if (haveLast_ && uint32_t(now-lastMs_)>120) lock();
    lastMs_=now; haveLast_=true;
    if (state_==Gripping || onsetPending_) state_=DirectionOnly;
    neutralPending_=onsetPending_=releasePending_=false;
    direction_=0;
  }
  bool needsRearm(char posture) const {
    return state_==Locked || (state_==DirectionOnly && posture=='n');
  }
  bool ready() const { return enabled_ && state_==Ready; }
  char update(const Input& in) {
    const bool gap = haveLast_ && uint32_t(in.ms-lastMs_)>120;
    lastMs_ = in.ms;
    haveLast_ = true;
    if (!enabled_ || !in.valid || !in.worn || gap ||
        !isfinite(in.envelope) || in.envelope<0 ||
        (in.posture && in.posture!='n' && in.posture!='w' && in.posture!='s' &&
         in.posture!='f' && in.posture!='b')) {
      lock();
      return 0;
    }
    if (!in.stable) {
      if (state_==Gripping || onsetPending_) state_=DirectionOnly;
      // 抬腕会带动肌电升高：取消旧握拳，但保留停稳后识别方向的资格。
      neutralPending_=onsetPending_=releasePending_=false;
      directionAt_=0;
      return 0;
    }
    const bool neutral = in.posture=='n';
    if (state_ == Locked || (state_==DirectionOnly && neutral)) {
      directionAt_=0;
      if (neutral && in.envelope<=thresholds_.off) {
        if (!neutralPending_) { neutralAt_=in.ms; neutralPending_=true; }
        if (uint32_t(in.ms-neutralAt_)>=REARM_MS) {
          state_=Ready; neutralPending_=false;
        }
      } else neutralPending_=false;
      return 0;
    }
    if ((state_==Gripping || onsetPending_) && !neutral) {
      state_=DirectionOnly;
      neutralPending_=onsetPending_=releasePending_=false;
      directionAt_=0;
    }
    if (state_ == Gripping) {
      if (in.envelope<=thresholds_.off) {
        if (!releasePending_) { releaseAt_=in.ms; releasePending_=true; }
        if (uint32_t(in.ms-releaseAt_)>=RELEASE_MS) {
          const uint32_t held=releaseAt_-gripAt_;
          lock();
          return held>=LONG_MS ? 'x' : (held>=240 ? ' ' : 0);
        }
        return 0;
      }
      releasePending_=false;
      if (uint32_t(in.ms-gripAt_)>=LONG_MS) { lock(); return 'x'; }
      return 0;
    }

    const char direction=neutral ? 0 : in.posture;
    if (!neutral) {
      neutralPending_=onsetPending_=false;
      if (!direction) { directionAt_=0; return 0; }
      if (direction!=directionAt_) { direction_=direction; directionAt_=in.ms; }
      else if (uint32_t(in.ms-directionAt_)>=DIRECTION_MS) {
        lock();
        return direction;
      }
      return 0;
    }
    directionAt_=0;
    if (in.envelope>=thresholds_.on) {
      if (!onsetPending_) { gripAt_=in.ms; onsetPending_=true; }
      if (uint32_t(in.ms-gripAt_)>=ONSET_MS) {
        state_=Gripping; releasePending_=false;
      }
    } else onsetPending_=false;
    return 0;
  }
  const char* stateName() const {
    if (!enabled_) return "请先校准";
    return state_==Locked ? "回位并放松" : state_==DirectionOnly ? "停稳选方向或回位" :
        state_==Ready ? "可以操作" : "握住或松开";
  }
  uint32_t holdMs() const { return state_==Gripping ? lastMs_-gripAt_ : 0; }
 private:
  enum State { Locked, Ready, Gripping, DirectionOnly };
  State state_ = Locked;
  Thresholds thresholds_ = {};
  bool enabled_=false, haveLast_=false, neutralPending_=false;
  bool onsetPending_=false, releasePending_=false;
  uint32_t lastMs_=0, neutralAt_=0, gripAt_=0, releaseAt_=0, directionAt_=0;
  char direction_=0;
  void lock() {
    state_=Locked;
    neutralPending_=onsetPending_=releasePending_=false;
    direction_=0;
  }
};
}
