#pragma once
#include <cmath>
#include <algorithm>
#include <stddef.h>
namespace fusion_features {
struct Sample { float ax,ay,az,gx,gy,gz,emg; };
inline void stats(const double* a,unsigned n,float* f) {
 double sum=0,mn=a[0],mx=a[0];for(unsigned i=0;i<n;++i){sum+=a[i];mn=std::min(mn,a[i]);mx=std::max(mx,a[i]);}
 double mean=sum/n,ss=0;for(unsigned i=0;i<n;++i)ss+=(a[i]-mean)*(a[i]-mean);
 f[0]=mean;f[1]=std::sqrt(ss/n);f[2]=mn;f[3]=mx;f[4]=mx-mn;
}
inline double quantile(const double* a,double q){double p=q*99;unsigned i=(unsigned)p;return a[i]+(a[i<99?i+1:i]-a[i])*(p-i);}
inline void emg(const Sample* s,float* f) {
 double a[100],sum=0,ss=0,change=0,early=0,late=0,first=0,last=0;unsigned high=0;
 for(unsigned i=0;i<100;++i){a[i]=s[i].emg;sum+=a[i];ss+=a[i]*a[i];if(i)change+=std::fabs(a[i]-a[i-1]);if(i<50)early+=a[i];else late+=a[i];if(i<25)first+=a[i];if(i>=75)last+=a[i];if(a[i]>10)++high;}
 double mean=sum/100,var=0;for(double v:a)var+=(v-mean)*(v-mean);
 std::sort(a,a+100);
 f[0]=mean;f[1]=std::sqrt(var/100);f[2]=a[0];f[3]=a[99];f[4]=quantile(a,.1);f[5]=quantile(a,.25);f[6]=quantile(a,.5);f[7]=quantile(a,.75);f[8]=quantile(a,.9);f[9]=std::sqrt(ss/100);f[10]=a[99]-a[0];f[11]=change/99;f[12]=late/50-early/50;f[13]=high/100.f;f[14]=last/25;f[15]=first/25;
}
inline void imu(const Sample* s,unsigned n,float* f) {
 double values[200];
 for(unsigned k=0;k<6;++k){for(unsigned i=0;i<n;++i){const Sample& a=s[i];switch(k){case 0:values[i]=a.ax;break;case 1:values[i]=a.ay;break;case 2:values[i]=a.az;break;case 3:values[i]=a.gx;break;case 4:values[i]=a.gy;break;default:values[i]=a.gz;}}
 stats(values,n,f+k*5);}
 for(unsigned i=0;i<n;++i){double x=s[i].ax,y=s[i].ay,z=s[i].az;values[i]=std::atan2(x,std::sqrt(y*y+z*z))*57.29577951308232;}stats(values,n,f+30);
 for(unsigned i=0;i<n;++i){values[i]=std::atan2(double(s[i].ay),double(s[i].az))*57.29577951308232;}
 stats(values,n,f+35);
}
}
