#pragma once
#include <cstring>
// 128x64 OLED presentation layout. Uses the same U8g2 fonts in firmware and preview.
// No sensor, recognition, timing, networking or audio behavior lives here.
namespace demo_ui {
template<class Display> void centered(Display& d,const char* text,int y,int maxWidth) {
 char fit[96]={};size_t n=std::strlen(text);if(n>95)n=95;
 while(n && (static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;
 std::memcpy(fit,text,n);
 while(n && d.getUTF8Width(fit)>maxWidth){
  --n;while(n && (static_cast<unsigned char>(fit[n])&0xc0)==0x80)--n;fit[n]=0;
 }
 d.drawUTF8((128-d.getUTF8Width(fit))/2,y,fit);
}
template<class Display> void drawSlice(Display& d,unsigned row,const char* title,const char* message,const char* footer,const char* badge) {
 d.setDrawColor(1);d.setFontPosBaseline();
 if(row==0){
  // Small voice symbol, title, and an inverted status chip.
  d.drawBox(3,6,3,5);d.drawLine(6,6,9,3);d.drawLine(6,10,9,13);d.drawVLine(9,3,11);
  d.drawLine(12,5,14,7);d.drawLine(14,7,14,9);d.drawLine(14,9,12,11);
  d.setFont(u8g2_font_wqy12_t_gb2312);d.drawUTF8(20,12,title);
  d.drawRBox(91,1,36,15,3);d.setDrawColor(0);
  d.drawUTF8(109-d.getUTF8Width(badge)/2,12,badge);d.setDrawColor(1);
  d.drawHLine(0,18,128);
 } else if(row==1){
  d.drawRFrame(2,23,124,27,4);
  d.setFont(u8g2_font_wqy16_t_gb2312);centered(d,message,43,116);
 } else if(row==2){
  d.setFont(u8g2_font_wqy12_t_gb2312);centered(d,footer,63,124);
 }
 d.setDrawColor(1);
}
} // namespace demo_ui
