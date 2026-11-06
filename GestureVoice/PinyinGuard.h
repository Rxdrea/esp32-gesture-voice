#pragma once
#include <cstring>
#include <stdint.h>
#include "PinyinTable.h"
namespace pinyin_guard {
inline bool next(const char*& p,uint32_t& cp){
 if(!*p)return false;
 const unsigned char a=static_cast<unsigned char>(*p++);
 if(a<128){cp=a;return true;}
 unsigned n=0;uint32_t minimum=0;
 if(a>=0xC2&&a<=0xDF){n=1;cp=a&31;minimum=128;}
 else if(a>=0xE0&&a<=0xEF){n=2;cp=a&15;minimum=2048;}
 else if(a>=0xF0&&a<=0xF4){n=3;cp=a&7;minimum=65536;}
 else return false;
 while(n--){unsigned char b=static_cast<unsigned char>(*p);if((b&0xC0)!=0x80)return false;++p;cp=(cp<<6)|(b&63);}
 return cp>=minimum&&cp<=0x10FFFF&&!(cp>=0xD800&&cp<=0xDFFF);
}
inline bool han(uint32_t cp){return cp>=0x4E00&&cp<=0x9FFF;}
inline bool punctuation(uint32_t cp){return cp==' '||cp==','||cp=='.'||cp=='?'||cp=='!'||cp==0x3000||cp==0x3001||cp==0x3002||cp==0xFF0C||cp==0xFF1F||cp==0xFF01||cp==0x2014||cp==0x2026;}
inline bool reading(uint32_t cp,const char* token){
 unsigned lo=0,hi=pinyin_table::COUNT;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(pinyin_table::entries[mid].code<cp)lo=mid+1;else hi=mid;}
 if(lo==pinyin_table::COUNT||pinyin_table::entries[lo].code!=cp)return false;
 const char* p=pinyin_table::readings+pinyin_table::entries[lo].offset;const size_t n=std::strlen(token);
 while(*p){if(*p++!='|')continue;const char* begin=p;while(*p&&*p!='|')++p;size_t len=p-begin;
  bool initial=n==1 || !std::strcmp(token,"zh") || !std::strcmp(token,"ch") || !std::strcmp(token,"sh");
  if((initial?len>=n:len==n)&&!std::strncmp(begin,token,n))return true;
 }
 return false;
}
inline bool match(const char* input,const char* text){
 const char* p=text;const char* q=input;unsigned count=0;
 while(*q){
  if(++count>32)return false;char token[8]={};unsigned n=0;
  while(*q&&*q!='\''){char c=*q++;if(c>='A'&&c<='Z')c+=32;if(c<'a'||c>'z'||n>=7)return false;token[n++]=c;}
  if(!n)return false;uint32_t cp=0;if(!next(p,cp)||!han(cp)||!reading(cp,token))return false;
  if(*q=='\''){++q;if(!*q)return false;}
 }
 if(!count)return false;
 while(*p){uint32_t cp=0;if(!next(p,cp)||!punctuation(cp))return false;}
 return true;
}
inline bool chineseText(const char* text){
 bool seen=false;const char* p=text;
 while(*p){uint32_t cp=0;if(!next(p,cp))return false;if(han(cp))seen=true;else if(!punctuation(cp))return false;}
 return seen;
}
inline bool completion(const char* text,const char* matched){
 const size_t n=std::strlen(matched);
 return n>0&&!std::strncmp(text,matched,n)&&std::strlen(text)>n&&chineseText(text)&&!std::strstr(text,"继续输入")&&!std::strstr(text,"补充拼音");
}
}
