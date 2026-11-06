#pragma once
// 完整六轴时序，与训练脚本使用相同的裁剪、插值和距离定义。
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
namespace demo_features {
constexpr unsigned POINTS=64, AXES=6, BAND=8;
constexpr float ACTIVE_DPS=35.f;
struct Sample { uint32_t us; float v[AXES]; };
using Pattern=float[POINTS][AXES];
inline float rate2(const Sample& s){return s.v[3]*s.v[3]+s.v[4]*s.v[4]+s.v[5]*s.v[5];}
inline bool extract(const Sample* s,unsigned n,Pattern& out,float& durationMs){
 if(n<2)return false;
 unsigned first=n,last=0;
 for(unsigned i=0;i<n;++i){
  for(float v:s[i].v)if(!std::isfinite(v))return false;
  if(i && (uint32_t(s[i].us-s[i-1].us)==0 || uint32_t(s[i].us-s[i-1].us)>40000))return false;
  if(rate2(s[i])>ACTIVE_DPS*ACTIVE_DPS){if(first==n)first=i;last=i;}
 }
 if(first>=last)return false;
 uint32_t span=s[last].us-s[first].us;durationMs=span*.001f;
 if(durationMs<500 || durationMs>5500)return false;
 unsigned j=first;
 for(unsigned k=0;k<POINTS;++k){
  double at=double(span)*k/(POINTS-1);
  while(j+1<last && double(uint32_t(s[j+1].us-s[first].us))<at)++j;
  double lo=uint32_t(s[j].us-s[first].us),hi=uint32_t(s[j+1].us-s[first].us);
  double t=(at-lo)/(hi-lo);
  for(unsigned a=0;a<AXES;++a)out[k][a]=float(s[j].v[a]+t*(s[j+1].v[a]-s[j].v[a]));
 }
 for(unsigned a=0;a<3;++a){float mean=0;for(unsigned k=0;k<POINTS;++k)mean+=out[k][a];mean/=POINTS;for(unsigned k=0;k<POINTS;++k)out[k][a]-=mean;}
 for(unsigned k=0;k<POINTS;++k)for(unsigned a=3;a<AXES;++a)out[k][a]/=150.f;
 return true;
}
inline float distance(const Pattern& a,const Pattern& b){
 // 带宽约束DTW，只使用两行固定工作区。
 float previous[POINTS+1],current[POINTS+1];
 const float inf=std::numeric_limits<float>::infinity();
 std::fill(previous,previous+POINTS+1,inf);previous[0]=0;
 for(unsigned i=1;i<=POINTS;++i){
  std::fill(current,current+POINTS+1,inf);
  unsigned begin=i>BAND?i-BAND:1,end=std::min(POINTS,i+BAND);
  for(unsigned j=begin;j<=end;++j){float cost=0;for(unsigned c=0;c<AXES;++c){float d=a[i-1][c]-b[j-1][c];cost+=d*d;}
   current[j]=cost+std::min(previous[j],std::min(previous[j-1],current[j-1]));}
  std::copy(current,current+POINTS+1,previous);
 }
 return previous[POINTS]/(POINTS*AXES);
}
}
