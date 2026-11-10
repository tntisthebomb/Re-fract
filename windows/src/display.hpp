#pragma once
// Draws textures (CPU RGB uploads, GPU renderer targets, the UI panel) into the window.
#include "gl_loader.hpp"
#include "engine.hpp"
#include <cstdio>
#include <string>
namespace rf {
class Display {
 unsigned program=0,vao=0,panelTexture=0,cpuTexture[2]={0,0};int cpuW=0,cpuH=0;
 int uT0=-1,uT1=-1,uMode=-1,uScale=-1,uFlip=-1;
 static unsigned compile(unsigned type,const char* src,std::string& error){
  unsigned s=glCreateShader(type);glShaderSource(s,1,&src,nullptr);glCompileShader(s);int ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){char log[1024]={};glGetShaderInfoLog(s,sizeof(log),nullptr,log);error=log;}return s;
 }
 void texture(unsigned& t,int w,int h,const void* data){
  if(!t){glGenTextures(1,&t);}
  glBindTexture(GL_TEXTURE_2D,t);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,data);
 }
 public:
 bool init(std::string& error){
  const char* vs="#version 330 core\nout vec2 uv;void main(){vec2 p=vec2(gl_VertexID&1,gl_VertexID>>1);uv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}";
  const char* fs="#version 330 core\nin vec2 uv;out vec4 color;uniform sampler2D t0,t1;uniform int mode,flip;uniform vec2 scale;\n"
   "void main(){vec2 c=vec2(uv.x,flip==1?1.0-uv.y:uv.y)*scale;vec3 a=texture(t0,c).rgb;\n"
   "if(mode==1){vec3 b=texture(t1,c).rgb;a=vec3(a.r,b.g,b.b);}color=vec4(a,1.0);}";
  unsigned v=compile(GL_VERTEX_SHADER,vs,error),f=compile(GL_FRAGMENT_SHADER,fs,error);
  program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
  int ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);glDeleteShader(v);glDeleteShader(f);
  if(!ok){char log[1024]={};glGetProgramInfoLog(program,sizeof(log),nullptr,log);error=log;return false;}
  uT0=glGetUniformLocation(program,"t0");uT1=glGetUniformLocation(program,"t1");uMode=glGetUniformLocation(program,"mode");
  uScale=glGetUniformLocation(program,"scale");uFlip=glGetUniformLocation(program,"flip");
  glGenVertexArrays(1,&vao);return true;
 }
 void clear(int w,int h){glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glClearColor(.06f,.06f,.055f,1);glClear(GL_COLOR_BUFFER_BIT);}
 // Uploads top-down RGB; eye slots 0/1. Reallocates when the size changes.
 void upload(int eye,const std::vector<Color>& pixels,int w,int h){
  if(w!=cpuW||h!=cpuH){for(int i=0;i<2;++i)if(cpuTexture[i]){glDeleteTextures(1,&cpuTexture[i]);cpuTexture[i]=0;}cpuW=w;cpuH=h;}
  {bool fresh=!cpuTexture[eye];
   if(fresh)texture(cpuTexture[eye],w,h,pixels.data());
   else{glBindTexture(GL_TEXTURE_2D,cpuTexture[eye]);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());}
  }
 }
 unsigned cpu(int eye)const{return cpuTexture[eye];}
 void uploadPanel(const std::vector<Color>& pixels,int w,int h){
  bool fresh=!panelTexture;if(fresh)texture(panelTexture,w,h,pixels.data());
  else{glBindTexture(GL_TEXTURE_2D,panelTexture);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());}
 }
 // x,y are top-left window coordinates in drawable pixels; windowHeight converts to GL's bottom-left origin.
 void draw(unsigned t0,unsigned t1,int mode,float sx,float sy,bool flip,bool linear,int x,int y,int w,int h,int windowHeight){
  if(w<=0||h<=0)return;
  glViewport(x,windowHeight-y-h,w,h);glUseProgram(program);glBindVertexArray(vao);
  glUniform1i(uT0,0);glUniform1i(uT1,1);glUniform1i(uMode,mode);glUniform2f(uScale,sx,sy);glUniform1i(uFlip,flip?1:0);
  unsigned ts[2]={t0,t1};int filter=linear?GL_LINEAR:GL_NEAREST;
  for(int i=1;i>=0;--i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,ts[i]?ts[i]:t0);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);}
  glDrawArrays(GL_TRIANGLE_STRIP,0,4);
 }
 void drawPanel(int x,int y,int w,int h,int windowHeight){draw(panelTexture,0,0,1,1,true,false,x,y,w,h,windowHeight);}
 // Aspect-preserving fit of an image inside a rectangle.
 static void fit(int rx,int ry,int rw,int rh,int iw,int ih,int& x,int& y,int& w,int& h){
  double s=std::min(double(rw)/iw,double(rh)/ih);w=std::max(1,int(iw*s));h=std::max(1,int(ih*s));x=rx+(rw-w)/2;y=ry+(rh-h)/2;
 }
};
}
