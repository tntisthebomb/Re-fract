#include "ui.hpp"
#include <cctype>
#include <cstdio>
#include <algorithm>

namespace rf {
Vec hsvColor(float h,float s,float v){
 h=h-std::floor(h);s=clamp(s,0,1);v=clamp(v,0,1);float x=h*6;int i=int(x);float f=x-i,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));
 switch(i){case 0:return {v,t,p};case 1:return {q,v,p};case 2:return {p,v,t};case 3:return {p,q,v};case 4:return {t,p,v};default:return {v,p,q};}
}
Vec colorHSV(Vec c){float hi=std::fmax(c.x,std::fmax(c.y,c.z)),lo=std::fmin(c.x,std::fmin(c.y,c.z)),d=hi-lo,h=0;
 if(d>1e-6f){h=hi==c.x?(c.y-c.z)/d:hi==c.y?2+(c.z-c.x)/d:4+(c.x-c.y)/d;h/=6;if(h<0)h+=1;}
 return {h,hi>0?d/hi:0,hi};
}
void drawColorPicker(Canvas& c,float h,float s,float v){
 c.rect(0,0,320,240,{24,22,19});c.text(12,8,"COLOR WHEEL",{230,220,190},2);
 auto rgb=[](Vec p){return Color{uint8_t(p.x*255+.5f),uint8_t(p.y*255+.5f),uint8_t(p.z*255+.5f)};};
 for(int y=36;y<=196;++y)for(int x=12;x<=172;++x){float dx=x-92,dy=y-116,r=std::sqrt(dx*dx+dy*dy);if(r<=80)c.point(x,y,rgb(hsvColor(std::atan2(dy,dx)/6.2831853f,r/80,v)));}
 for(int y=45;y<=180;++y)c.rect(196,y,30,1,rgb(hsvColor(h,s,1-float(y-45)/135)));
 int mx=92+int(78*s*std::cos(h*6.2831853f)),my=116+int(78*s*std::sin(h*6.2831853f));c.rect(mx-3,my-3,7,7,{255,255,255});c.rect(mx-1,my-1,3,3,{0,0,0});
 int by=45+int((1-v)*135);c.rect(193,by-1,36,3,{255,255,255});c.rect(244,48,58,58,rgb(hsvColor(h,s,v)));
 c.text(188,190,"BRIGHTNESS",{230,220,190});c.text(12,204,"PAD: HUE / SAT   L/R: VALUE",{230,220,190});
 c.rect(12,222,130,16,{95,75,40});c.text(22,226,"B CANCEL",{255,240,200});c.rect(176,222,132,16,{95,75,40});c.text(186,226,"A APPLY",{255,240,200});
}

namespace {
// Hand-drawn 5x7 uppercase stencil. Bits are horizontal, MSB at the left.
const char* glyph(char c){
 switch(c){
 case 'A':return "\x0e\x11\x11\x1f\x11\x11\x11";
 case 'B':return "\x1e\x11\x11\x1e\x11\x11\x1e";
 case 'C':return "\x0f\x10\x10\x10\x10\x10\x0f";
 case 'D':return "\x1e\x11\x11\x11\x11\x11\x1e";
 case 'E':return "\x1f\x10\x10\x1e\x10\x10\x1f";
 case 'F':return "\x1f\x10\x10\x1e\x10\x10\x10";
 case 'G':return "\x0f\x10\x10\x13\x11\x11\x0f";
 case 'H':return "\x11\x11\x11\x1f\x11\x11\x11";
 case 'I':return "\x0e\x04\x04\x04\x04\x04\x0e";
 case 'J':return "\x07\x02\x02\x02\x12\x12\x0c";
 case 'K':return "\x11\x12\x14\x18\x14\x12\x11";
 case 'L':return "\x10\x10\x10\x10\x10\x10\x1f";
 case 'M':return "\x11\x1b\x15\x15\x11\x11\x11";
 case 'N':return "\x11\x19\x15\x13\x11\x11\x11";
 case 'O':return "\x0e\x11\x11\x11\x11\x11\x0e";
 case 'P':return "\x1e\x11\x11\x1e\x10\x10\x10";
 case 'Q':return "\x0e\x11\x11\x11\x15\x12\x0d";
 case 'R':return "\x1e\x11\x11\x1e\x14\x12\x11";
 case 'S':return "\x0f\x10\x10\x0e\x01\x01\x1e";
 case 'T':return "\x1f\x04\x04\x04\x04\x04\x04";
 case 'U':return "\x11\x11\x11\x11\x11\x11\x0e";
 case 'V':return "\x11\x11\x11\x11\x11\x0a\x04";
 case 'W':return "\x11\x11\x11\x15\x15\x15\x0a";
 case 'X':return "\x11\x11\x0a\x04\x0a\x11\x11";
 case 'Y':return "\x11\x11\x0a\x04\x04\x04\x04";
 case 'Z':return "\x1f\x01\x02\x04\x08\x10\x1f";
 case '0':return "\x0e\x11\x13\x15\x19\x11\x0e";
 case '1':return "\x04\x0c\x04\x04\x04\x04\x0e";
 case '2':return "\x0e\x11\x01\x02\x04\x08\x1f";
 case '3':return "\x1e\x01\x01\x0e\x01\x01\x1e";
 case '4':return "\x02\x06\x0a\x12\x1f\x02\x02";
 case '5':return "\x1f\x10\x10\x1e\x01\x01\x1e";
 case '6':return "\x0e\x10\x10\x1e\x11\x11\x0e";
 case '7':return "\x1f\x01\x02\x04\x08\x08\x08";
 case '8':return "\x0e\x11\x11\x0e\x11\x11\x0e";
 case '9':return "\x0e\x11\x11\x0f\x01\x01\x0e";
 case '.':return "\x00\x00\x00\x00\x00\x0c\x0c";
 case ':':return "\x00\x0c\x0c\x00\x0c\x0c\x00";
 case '-':return "\x00\x00\x00\x1f\x00\x00\x00";
 case '+':return "\x00\x04\x04\x1f\x04\x04\x00";
 case '/':return "\x01\x01\x02\x04\x08\x10\x10";
 case '>':return "\x10\x08\x04\x02\x04\x08\x10";
 case '<':return "\x01\x02\x04\x08\x04\x02\x01";
 case '=':return "\x00\x00\x1f\x00\x1f\x00\x00";
 case '*':return "\x00\x15\x0e\x1f\x0e\x15\x00";
 case '^':return "\x04\x0a\x11\x00\x00\x00\x00";
 case '(':return "\x02\x04\x08\x08\x08\x04\x02";
 case ')':return "\x08\x04\x02\x02\x02\x04\x08";
 case ',':return "\x00\x00\x00\x00\x04\x04\x08";
 case '_':return "\x00\x00\x00\x00\x00\x00\x1f";
 case '!':return "\x04\x04\x04\x04\x04\x00\x04";
 case '%':return "\x19\x1a\x02\x04\x08\x0b\x13";
 default:return "\x00\x00\x00\x00\x00\x00\x00";
 }
}
}
void Canvas::point(int x,int y,Color c){if(x>=0&&x<width&&y>=0&&y<height)pixels[y*width+x]=c;}
void Canvas::rect(int x,int y,int w,int h,Color c){for(int yy=std::max(0,y);yy<std::min(height,y+h);++yy)for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx)point(xx,yy,c);}
void Canvas::text(int x,int y,const std::string& s,Color color,int scale){
 int begin=x;for(unsigned char ch:s){if(ch=='\n'){x=begin;y+=8*scale;continue;}const char* g=glyph(char(std::toupper(ch)));
  for(int row=0;row<7;++row)for(int bit=0;bit<5;++bit)if(g[row]&(1<<(4-bit)))rect(x+bit*scale,y+row*scale,scale,scale,color);
  x+=6*scale;
 }
}
void Canvas::grunge(){
 if(!texture.empty()){pixels=texture;return;}
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){uint32_t n=uint32_t(x*374761393u)^uint32_t(y*668265263u);n=(n^(n>>13))*1274126177u;
  int v=18+((n>>27)&7)+(y%3==0?0:3);point(x,y,{uint8_t(v+2),uint8_t(v+1),uint8_t(v-2)});
 }
 for(int i=0;i<21;++i){int x=(i*83+17)%width,y=(i*47+3)%height;rect(x,y,15+(i%5)*7,1,{49,43,32});}
 texture=pixels;
}
void drawPanel(Canvas& c,int tab,int selected,const std::vector<Row>& rows,const std::string& status,
 const std::string& name,int block,float progress,bool quality,float workMs){
 c.grunge();Color bone{215,209,177},rust{199,89,40},dim{127,127,109};
 c.rect(6,5,308,26,{36,32,24});c.rect(6,5,4,26,rust);c.text(15,8,"RE-FRACT",bone,2);
 c.text(190,9,quality?"QUALITY / STILL":"LIVE / INTERLACED",rust);
 char perf[40];std::snprintf(perf,sizeof(perf),"%dX  %.1F MS  %d%%",block,workMs,int(progress*100));c.text(190,21,perf,dim);
 const char* tabs[]={"PRESET","RENDER","FORMULA","STAGE","COLOR","FILES"};
 for(int i=0;i<6;++i){int x=6+i*52;c.rect(x,36,50,17,i==tab?rust:Color{45,42,34});c.text(x+4,41,tabs[i],i==tab?Color{21,20,17}:bone);}
 c.text(10,59,name.substr(0,37),dim);
 char page[20];std::snprintf(page,sizeof(page),"%d/%d",selected/8+1,int((rows.size()+7)/8));c.text(280,59,page,rust);
 int first=(selected/8)*8;
 for(int j=0;j<8&&first+j<int(rows.size());++j){int i=first+j,y=73+j*15;
  if(i==selected){c.rect(6,y-3,308,14,{66,48,30});c.rect(6,y-3,3,14,rust);}
  c.text(13,y,rows[i].label.substr(0,24),i==selected?bone:dim);
  c.text(170,y,rows[i].value.substr(0,23),bone);
 }
 c.rect(6,196,308,1,rust);c.text(10,202,status.substr(0,50),rust);
 c.text(10,215,"D-PAD EDIT  A TYPE  SELECT TAB",bone);
 c.text(10,228,"Y QUALITY  B RESET VIEW  START EXIT",dim);
}
}
