#pragma once
// Thin platform shim: replaces libctru (time, HID, touch) with SDL2 keyboard + mouse.
#include <SDL.h>
#include <cstdint>
#include <cstdlib>
namespace rf {
enum : uint32_t {KEY_A=1,KEY_B=2,KEY_SELECT=4,KEY_START=8,KEY_DRIGHT=16,KEY_DLEFT=32,KEY_DUP=64,KEY_DDOWN=128,
 KEY_R=256,KEY_L=512,KEY_X=1024,KEY_Y=2048,KEY_ZL=4096,KEY_ZR=8192,KEY_TOUCH=16384};
inline double nowMs(){static const uint64_t f=SDL_GetPerformanceFrequency(),t0=SDL_GetPerformanceCounter();return double(SDL_GetPerformanceCounter()-t0)*1000.0/double(f);}
struct Input {
 uint32_t down=0,held=0;
 float circleX=0,circleY=0,stickX=0,stickY=0; // 3DS circle pad / C-stick, -1..1 (up = +y)
 int mouseX=0,mouseY=0,clickX=0,clickY=0;     // window coordinates
 float lookX=0,lookY=0;                        // right-drag mouse look, pixels this frame
 float wheel=0;int strengthStep=0;             // stereo strength keys [ and ]
 bool quit=false,togglePanel=false,fullscreen=false,screenshot=false;
};
class Platform {
 uint32_t previous=0;bool relative=false;
 static uint32_t map(SDL_Scancode s){
  switch(s){
  case SDL_SCANCODE_RETURN:case SDL_SCANCODE_KP_ENTER:return KEY_A;
  case SDL_SCANCODE_BACKSPACE:case SDL_SCANCODE_B:return KEY_B;
  case SDL_SCANCODE_Y:return KEY_Y;case SDL_SCANCODE_LSHIFT:case SDL_SCANCODE_RSHIFT:return KEY_X;
  case SDL_SCANCODE_SPACE:return KEY_SELECT;case SDL_SCANCODE_ESCAPE:return KEY_START;
  case SDL_SCANCODE_I:case SDL_SCANCODE_KP_8:return KEY_DUP;case SDL_SCANCODE_K:case SDL_SCANCODE_KP_2:return KEY_DDOWN;
  case SDL_SCANCODE_J:case SDL_SCANCODE_KP_4:return KEY_DLEFT;case SDL_SCANCODE_L:case SDL_SCANCODE_KP_6:return KEY_DRIGHT;
  case SDL_SCANCODE_Q:return KEY_L;case SDL_SCANCODE_E:return KEY_R;
  case SDL_SCANCODE_Z:return KEY_ZL;case SDL_SCANCODE_C:return KEY_ZR;
  default:return 0;}
 }
 static uint32_t heldKeys(){const Uint8* k=SDL_GetKeyboardState(nullptr);uint32_t held=0;for(int s=0;s<SDL_NUM_SCANCODES;++s)if(k[s])held|=map(SDL_Scancode(s));return held;}
 public:
 // Call after a modal loop so keys still held from it do not register as new presses.
 void absorb(){SDL_PumpEvents();previous=heldKeys();}
 SDL_Window* window=nullptr;SDL_GLContext context=nullptr;
 bool fullscreen=false;
 void poll(Input& in){
  in.down=0;in.lookX=in.lookY=0;in.wheel=0;in.strengthStep=0;in.togglePanel=in.fullscreen=in.screenshot=false;
  uint32_t click=0;SDL_Event e;
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT)in.quit=true;
   else if(e.type==SDL_KEYDOWN&&!e.key.repeat){
    SDL_Scancode s=e.key.keysym.scancode;in.down|=map(s);
    if(s==SDL_SCANCODE_TAB)in.togglePanel=true;else if(s==SDL_SCANCODE_F11)in.fullscreen=true;else if(s==SDL_SCANCODE_F12)in.screenshot=true;
    else if(s==SDL_SCANCODE_LEFTBRACKET)in.strengthStep=-1;else if(s==SDL_SCANCODE_RIGHTBRACKET)in.strengthStep=1;
   }else if(e.type==SDL_MOUSEBUTTONDOWN){
    if(e.button.button==SDL_BUTTON_LEFT){click=KEY_TOUCH;in.clickX=e.button.x;in.clickY=e.button.y;}
    else if(e.button.button==SDL_BUTTON_RIGHT&&!relative){SDL_SetRelativeMouseMode(SDL_TRUE);relative=true;}
   }else if(e.type==SDL_MOUSEBUTTONUP&&e.button.button==SDL_BUTTON_RIGHT&&relative){SDL_SetRelativeMouseMode(SDL_FALSE);relative=false;}
   else if(e.type==SDL_MOUSEMOTION&&relative){in.lookX+=float(e.motion.xrel);in.lookY+=float(e.motion.yrel);}
   else if(e.type==SDL_MOUSEWHEEL)in.wheel+=float(e.wheel.y);
  }
  SDL_GetMouseState(&in.mouseX,&in.mouseY);
  const Uint8* k=SDL_GetKeyboardState(nullptr);uint32_t held=heldKeys();
  // Held bits never include the transient click; motion keys are read as analog axes.
  in.circleX=float(k[SDL_SCANCODE_D])-float(k[SDL_SCANCODE_A]);in.circleY=float(k[SDL_SCANCODE_W])-float(k[SDL_SCANCODE_S]);
  in.stickX=float(k[SDL_SCANCODE_RIGHT])-float(k[SDL_SCANCODE_LEFT]);in.stickY=float(k[SDL_SCANCODE_UP])-float(k[SDL_SCANCODE_DOWN]);
  in.down|=(held&~previous)|click;previous=held;in.held=held;
 }
 void setFullscreen(bool on){if(!window)return;fullscreen=on;SDL_SetWindowFullscreen(window,on?SDL_WINDOW_FULLSCREEN_DESKTOP:0);}
 void drawable(int& w,int& h){SDL_GL_GetDrawableSize(window,&w,&h);}
 void shutdown(){if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();context=nullptr;window=nullptr;}
};
}
