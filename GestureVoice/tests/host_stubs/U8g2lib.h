#pragma once
#include "Arduino.h"
#define U8G2_R0 0
#define U8X8_PIN_NONE 255
static const uint8_t u8g2_font_wqy12_t_gb2312[]={0};
static const uint8_t u8g2_font_logisoso16_tf[]={0};
namespace host { static unsigned oledConstructs=0,oledBegins=0; static std::vector<std::string> screenText; }
class U8G2_SSD1306_128X64_NONAME_F_HW_I2C {
 public:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C(int,int,int=-1,int=-1) { ++host::oledConstructs; }
  bool begin() { ++host::oledBegins; return true; }
  void setBusClock(unsigned long) {}
  void setI2CAddress(uint8_t) {}
  void enableUTF8Print() {}
  void clearBuffer() {}
  void setFont(const uint8_t*) {}
  void setFontPosTop() {}
  void setFontPosBaseline() {}
  void setCursor(int,int) {}
  template<class T> void print(const T& x) { std::ostringstream s; s<<x; host::screenText.push_back(s.str()); }
  template<class T> void print(const T& x,int) { print(x); }
  void sendBuffer() {}
  void firstPage() {}
  bool nextPage() { return false; }
  int getUTF8Width(const char* s) { int n=0; for(;*s;++s) if((static_cast<unsigned char>(*s)&0xC0)!=0x80) ++n; return n*6; }
  void drawUTF8(int,int,const char* s) { host::screenText.emplace_back(s); }
};
