// Re-fract for Windows (SDL2 + OpenGL 3.3). A port of source/main.cpp: the same menu, panel and
// control semantics, with the 3DS HID/touch/keyboard/screen layers replaced by platform.hpp + display.hpp.
#include "platform.hpp"
#include "display.hpp"
#include "workers.hpp"
#include "ui.hpp"
#include "gpu_wrapper.hpp"
#include "distance_field.hpp"
#include "trace_benchmark.hpp"
#include <chrono>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <cmath>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <memory>
#include <fstream>
#include <filesystem>
#include <thread>

using namespace rf;
namespace {
struct Field {Row row;std::function<void(int)> adjust;std::function<void()> activate;};
std::string number(float v){char b[40];std::snprintf(b,sizeof(b),"%.5G",v);return b;}
Stage newStage(Kind kind){Stage s;s.kind=kind;s.a=0;s.b=0;s.c=0;
 if(kind==Kind::KleinianFold)s.a=s.b=s.c=.8f;
 if(kind==Kind::Inversion)s.a=1;
 if(kind==Kind::BoxFold)s.a=1;
 if(kind==Kind::SphereFold){s.a=.5f;s.b=1;}
 if(kind==Kind::Bulb){s.a=8;s.b=s.c=1;}
 if(kind==Kind::Scale){s.a=2;s.b=1;}
 if(kind==Kind::Menger){s.a=3;s.b=s.c=1;}
 return s;
}
struct Resolution {const char* name;int w,h;};
const Resolution resolutions[]={{"400X240 (3DS)",400,240},{"640X360",640,360},{"854X480",854,480},{"1280X720",1280,720},{"1600X900",1600,900},
 {"1920X1080",1920,1080},{"2560X1440",2560,1440},{"3840X2160",3840,2160},{"MATCH WINDOW",0,0},{"CUSTOM",0,0}};
constexpr int ResolutionCount=10,MatchWindow=8,CustomResolution=9;
const int windowSizes[][2]={{1280,720},{1600,900},{1920,1080},{2560,1440}};
enum StereoMode {StereoOff,StereoSideBySide,StereoAnaglyph};
const char* stereoModeNames[]={"OFF","SIDE BY SIDE","ANAGLYPH R/C"};

std::string dataDirectory(){
 namespace fs=std::filesystem;std::error_code ec;fs::path dir;
#ifdef _WIN32
 const char* home=std::getenv("USERPROFILE");dir=fs::path(home?home:".")/"Documents"/"Re-fract";
#else
 const char* home=std::getenv("HOME");dir=fs::path(home?home:".")/"Re-fract";
#endif
 fs::create_directories(dir,ec);if(ec){dir=fs::path(".")/"Re-fract-files";fs::create_directories(dir,ec);}
 return dir.string();
}
bool writePPM(const std::vector<Color>& image,int w,int h,const std::string& path,std::string& error){
 if(image.size()!=size_t(w)*h){error="Invalid image size";return false;}
 std::ofstream out(path,std::ios::binary);if(!out){error="Cannot write image";return false;}
 out<<"P6\n"<<w<<' '<<h<<"\n255\n";out.write(reinterpret_cast<const char*>(image.data()),std::streamsize(image.size()*3));
 out.close();if(!out){error="Image write failed";return false;}error="IMAGE SAVED";return true;
}
struct Args {int tab=0,select=0,stereo=0;std::string windowShot;int frames=0,move=0,preset=-1,width=0,height=0,wheel=0;bool selftest=false,cpu=false,gpu=false;std::string screenshot,panel;};
Args parseArgs(int argc,char** argv){
 Args a;
 for(int i=1;i<argc;++i){std::string s=argv[i];auto next=[&]()->const char*{return i+1<argc?argv[++i]:"";};
  if(s=="--selftest"){a.selftest=true;a.cpu=true;if(!a.frames)a.frames=30;}
  else if(s=="--frames")a.frames=std::atoi(next());
  else if(s=="--screenshot")a.screenshot=next();
  else if(s=="--panel-shot")a.panel=next();
  else if(s=="--preset")a.preset=std::atoi(next());
  else if(s=="--tab")a.tab=std::atoi(next());
  else if(s=="--select")a.select=std::atoi(next());
  else if(s=="--wheel")a.wheel=std::atoi(next());
  else if(s=="--window-shot")a.windowShot=next();
  else if(s=="--stereo"){std::string m=next();a.stereo=m=="sbs"?1:m=="ana"?2:0;}
  else if(s=="--move")a.move=std::atoi(next());
  else if(s=="--backend"){std::string b=next();a.cpu=b=="cpu";a.gpu=b=="gpu";}
  else if(s=="--res"){std::sscanf(next(),"%dx%d",&a.width,&a.height);}
  else if(s=="--help"){std::printf("Re-fract [--backend cpu|gpu] [--res WxH] [--preset N] [--selftest] [--frames N] [--screenshot out.ppm] [--panel-shot out.ppm] [--move N] [--tab N --select N --wheel NOTCHES]\n");std::exit(0);}
 }
 return a;
}
// Limits (raised from the 3DS values; the engine validate()/settingsValid() accept the same ranges).
// Host memory still available (bytes), or 0 if unknown. Used to refuse render sizes that would exhaust RAM.
size_t availableMemory(){
#ifdef _WIN32
 MEMORYSTATUSEX m{};m.dwLength=sizeof(m);return GlobalMemoryStatusEx(&m)?size_t(std::min<unsigned long long>(m.ullAvailPhys,m.ullAvailVirtual)):0;
#else
 long pages=sysconf(_SC_AVPHYS_PAGES),size=sysconf(_SC_PAGE_SIZE);return pages>0&&size>0?size_t(pages)*size_t(size):0;
#endif
}
// Approximate host bytes per pixel: CPU renderer buffers (both eyes, shading records, lighting caches) / GPU readback and export copies.
constexpr size_t CpuBytesPerPixel=300,GpuBytesPerPixel=24;
}
int main(int argc,char** argv){
 Args args=parseArgs(argc,argv);bool headless=args.frames>0;
 if(SDL_Init(SDL_INIT_VIDEO)!=0){std::fprintf(stderr,"SDL_Init failed: %s\n",SDL_GetError());return 1;}
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
 Platform pf;int startW=1600,startH=900;
 {SDL_Rect r;if(SDL_GetDisplayBounds(0,&r)==0&&(r.w<1700||r.h<980)){startW=1280;startH=720;}}
 pf.window=SDL_CreateWindow("Re-fract",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,startW,startH,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
 if(pf.window)pf.context=SDL_GL_CreateContext(pf.window);
 if(!pf.window||!pf.context){std::fprintf(stderr,"Cannot create an OpenGL 3.3 core window: %s\n",SDL_GetError());return 1;}
 SDL_GL_SetSwapInterval(headless?0:1);
 if(!loadGL()){std::fprintf(stderr,"OpenGL 3.3 entry points are missing\n");return 1;}
 int maxTexture=4096;{GLint v=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&v);if(v>0)maxTexture=v;}
  Display display;{std::string error;if(!display.init(error)){std::fprintf(stderr,"Display shader failed: %s\n",error.c_str());return 1;}}
 WorkerPool pool;int threads=std::max(1u,std::thread::hardware_concurrency());pool.start(threads);
 const std::string dir=dataDirectory();

 auto sceneOwner=std::make_unique<Scene>();auto rendererOwner=std::make_unique<Renderer>();
 Scene& scene=*sceneOwner;Renderer& renderer=*rendererOwner;Canvas bottom(320,240);
 renderer.setBatchShader(WorkerPool::shade,&pool);renderer.setParallelFor(WorkerPool::parallelFor,&pool);
 scene.settings.batchSize=std::max(8,std::min(32,threads*2));
 std::shared_ptr<DistanceField> distanceField;std::unique_ptr<Scene> detailScene;std::unique_ptr<TraceBenchmark> benchmark;bool benchmarkActive=false;double benchmarkStart=0;
 Gpu gpu;bool useGpu=false,wantGpu=true,forceCpu=false;std::string fallbackReason;
 int tab=0,selected=0,presetIndex=0,stageIndex=0,slot=0,lightIndex=0;float slider=0,workMs=0;StereoSlider sliderControl;uint64_t sliderUntil=0;
 // PC display state.
 int resIndex=3,customW=1280,customH=720,motionDiv=1,stereoMode=StereoOff,panelScale=0;float stereoStrength=1;
 bool linear=false,panelVisible=true,applyPending=true,wantWindowShot=false;int shownW=0,shownH=0;
 bool screenshotRequested=false,exitRequested=false;float adjustScale=1;
 std::string status;stereoMode=args.stereo;tab=std::max(0,std::min(5,args.tab));selected=args.select;
 auto loadPreset=[&](int i){
  int keepBatch=scene.settings.batchSize;
  scene.formula=preset(i);presetIndex=i;stageIndex=0;scene.camera=presetCamera(i);
  if(i>=24){scene.settings=presetSettings(i);scene.settings.batchSize=keepBatch;}
  scene.settings.farClip=40;scene.settings.convergence=-scene.camera.position.z;
 };
 {std::string error;
  wantGpu=!args.cpu;
  if(wantGpu&&!gpu.init(error)){wantGpu=false;status="GPU UNAVAILABLE / CPU BACKEND";if(!error.empty())std::fprintf(stderr,"GPU init failed: %s\n",error.c_str());}
  if(!wantGpu){resIndex=0;if(status.empty())status="CPU BACKEND";}
  if(args.gpu&&!wantGpu){std::fprintf(stderr,"GPU backend requested but unavailable\n");}
  if(args.width>0&&args.height>0){customW=std::max(1,args.width);customH=std::max(1,args.height);resIndex=CustomResolution;}
  if(args.preset>=0&&args.preset<PresetCount)loadPreset(args.preset);
 }
 // Resolve the active render size (clamped to the GL texture limit and a memory budget for the active backend).
 int matchW=0,matchH=0;double matchSince=0;std::string sizeNote;
 auto targetSize=[&](int& w,int& h){
  if(resIndex==MatchWindow){w=matchW;h=matchH;}
  else if(resIndex==CustomResolution){w=customW;h=customH;}
  else{w=resolutions[resIndex].w;h=resolutions[resIndex].h;}
  int rw=w,rh=h;
  // No fixed size limits: only what OpenGL textures and host memory can actually hold.
  w=std::max(1,std::min(maxTexture,w));h=std::max(1,std::min(maxTexture,h));
  size_t per=useGpu?GpuBytesPerPixel:CpuBytesPerPixel,avail=availableMemory();
  if(avail&&double(w)*h*per>double(avail)*.7){double k=std::sqrt(double(avail)*.7/(double(w)*h*per));w=std::max(1,int(w*k));h=std::max(1,int(h*k));}
  sizeNote=(w!=rw||h!=rh)?(std::string(useGpu?"RESOLUTION LIMITED BY GPU/RAM TO ":"RESOLUTION LIMITED BY RAM TO ")+std::to_string(w)+"X"+std::to_string(h)):"";
 };
 std::vector<uint64_t> uploaded(2,0);bool uploadedStereo=false;
 auto applyBackend=[&](){
  int w,h;targetSize(w,h);std::string error;
  if(useGpu&&!gpu.ready()&&!gpu.init(error)){useGpu=false;wantGpu=false;status="GPU UNAVAILABLE / CPU BACKEND";targetSize(w,h);}
  if(useGpu){renderer.resize(1,1);W=w;H=h;
   if(!gpu.resize(w,h)){useGpu=false;forceCpu=true;status="GPU RESIZE FAILED / CPU BACKEND";targetSize(w,h);}}
  if(!useGpu){
   try{renderer.resize(w,h);}
   catch(const std::bad_alloc&){w=std::min(w,1280);h=std::min(h,720);W=w;H=h;renderer.resize(w,h);status="OUT OF MEMORY / FELL BACK TO 1280X720";sizeNote.clear();resIndex=3;}
   uploaded.assign(2,0);
  }
  if(!sizeNote.empty())status=sizeNote;
  shownW=W;shownH=H;
 };
 // Decide which backend renders the current scene. GPU only when the user prefers it and the scene is supported.
 std::string lastReason;
 auto reconcile=[&](){
  bool desired=false;std::string why;
  if(wantGpu&&!gpu.ready()){std::string e;if(!gpu.init(e)){wantGpu=false;status="GPU UNAVAILABLE / CPU BACKEND";}}
  if(wantGpu&&!forceCpu){desired=Gpu::supports(scene,why);}
  else if(wantGpu)why="GPU SHADER FAILED";
  fallbackReason=(wantGpu&&!desired)?why:"";
  if(desired!=useGpu){useGpu=desired;applyPending=true;status=desired?"GPU: "+std::string(gpu.name()):"CPU RENDER: "+why;}
  else if(wantGpu&&!desired&&why!=lastReason)status="CPU RENDER: "+why;
  lastReason=fallbackReason;
 };
 float gpuEye=-1;bool gpuDone=false,gpuMoving=false,gpuSeen=false;bool changed=false,menuDirty=true;
 uint64_t frameNumber=0;
 auto currentImage=[&](int eye,std::vector<Color>& out,int& w,int& h)->bool{
  if(useGpu)return gpu.readback(std::min(eye,gpu.eyes()-1),out,w,h);
  out=renderer.image(eye);w=W;h=H;return !out.empty();
 };
 struct Layout {int dw,dh,px,py,ps,vx,vy,vw,vh;};
 auto layout=[&](){Layout l{};pf.drawable(l.dw,l.dh);
  if(panelVisible){int cap=std::max(1,std::min(l.dh/240,l.dw/320));l.ps=panelScale?std::min(panelScale,cap):std::max(1,std::min(l.dh/240,(l.dw*2/5)/320));
   l.px=l.dw-320*l.ps;l.py=0;l.vw=std::max(1,l.px);}
  else{l.ps=0;l.px=l.dw;l.vw=l.dw;}
  l.vx=0;l.vy=0;l.vh=l.dh;return l;};
 // Window coordinates -> 320x240 panel (3DS touch) coordinates.
 auto toPanel=[&](int mx,int my,int& tx,int& ty){Layout lay=layout();int winW,winH;SDL_GetWindowSize(pf.window,&winW,&winH);
  tx=lay.ps?(mx*lay.dw/std::max(1,winW)-lay.px)/lay.ps:-1;ty=lay.ps?(my*lay.dh/std::max(1,winH)-lay.py)/lay.ps:-1;
  return lay.ps&&mx*lay.dw/std::max(1,winW)>=lay.px&&tx<320&&tx>=0&&ty>=0&&ty<240;};
 // Composes the window: render view (letterboxed, mono / side-by-side / anaglyph) + the 320x240 panel.
 auto present=[&](const Canvas& panel){
  Layout l=layout();display.clear(l.dw,l.dh);display.uploadPanel(panel.pixels,320,240);
  bool stereo=slider>0&&(useGpu?gpu.eyes()==2:uploadedStereo);
  unsigned t0=useGpu?gpu.texture(0):display.cpu(0),t1=useGpu?gpu.texture(stereo?1:0):display.cpu(stereo?1:0);
  float sx=useGpu?float(shownW)/std::max(1,W):1,sy=useGpu?float(shownH)/std::max(1,H):1;bool flip=!useGpu;
  if(t0){
   if(stereo&&stereoMode==StereoSideBySide){
    for(int e=0;e<2;++e){int x,y,w,h;Display::fit(l.vx+e*l.vw/2,l.vy,l.vw/2,l.vh,W,H,x,y,w,h);display.draw(e?t1:t0,0,0,sx,sy,flip,linear,x,y,w,h,l.dh);}
   }else{int x,y,w,h;Display::fit(l.vx,l.vy,l.vw,l.vh,W,H,x,y,w,h);display.draw(t0,t1,stereo&&stereoMode==StereoAnaglyph?1:0,sx,sy,flip,linear,x,y,w,h,l.dh);}
  }
  if(l.ps)display.drawPanel(l.px,l.py,320*l.ps,240*l.ps,l.dh);
  if(wantWindowShot){wantWindowShot=false;std::vector<Color> px(size_t(l.dw)*l.dh),flipped(px.size());glReadPixels(0,0,l.dw,l.dh,GL_RGB,GL_UNSIGNED_BYTE,px.data());
   for(int y=0;y<l.dh;++y)std::copy(px.begin()+size_t(l.dh-1-y)*l.dw,px.begin()+size_t(l.dh-y)*l.dw,flipped.begin()+size_t(y)*l.dw);
   std::string error;writePPM(flipped,l.dw,l.dh,args.windowShot,error);}
  SDL_GL_SwapWindow(pf.window);
 };
 // Replacement for the 3DS software keyboard: typed text, Enter confirms, Esc cancels, Ctrl+V pastes.
 auto keyboard=[&](const std::string& prompt,std::string& value)->bool{
  std::string text=value;SDL_StartTextInput();SDL_PumpEvents();SDL_FlushEvent(SDL_TEXTINPUT);bool result=false,running=true;
  while(running){
   SDL_Event e;
   while(SDL_PollEvent(&e)){
    if(e.type==SDL_QUIT){running=false;}
    else if(e.type==SDL_TEXTINPUT){if(text.size()<250)text+=e.text.text;}
    else if(e.type==SDL_KEYDOWN){SDL_Keycode k=e.key.keysym.sym;
     if(k==SDLK_RETURN||k==SDLK_KP_ENTER){bool blank=text.find_first_not_of(" \t")==std::string::npos;if(!blank){value=text;result=true;}running=false;}
     else if(k==SDLK_ESCAPE)running=false;
     else if(k==SDLK_BACKSPACE){if(!text.empty())text.pop_back();}
     else if(k==SDLK_v&&(e.key.keysym.mod&KMOD_CTRL)){char* clip=SDL_GetClipboardText();if(clip){text+=clip;SDL_free(clip);}}
    }
   }
   Canvas overlay=bottom;overlay.rect(8,92,304,64,{36,32,24});overlay.rect(8,92,304,2,{199,89,40});
   overlay.text(14,99,prompt.substr(0,48),{127,127,109});
   std::string shown=text.size()>46?text.substr(text.size()-46):text;
   overlay.text(14,115,shown+((SDL_GetTicks()/400)%2?"_":" "),{215,209,177});
   overlay.text(14,140,"ENTER OK  ESC CANCEL",{199,89,40});
   present(overlay);SDL_Delay(16);
  }
  SDL_StopTextInput();pf.absorb();return result;
 };
 // HSV colour wheel (port of the 3DS pickColor): mouse drag on the wheel / brightness bar, IJKL hue+saturation,
 // Q/E value, mouse wheel value, H types a hex colour, Enter applies, Esc/B cancels.
 auto pickColor=[&](const std::string& title,Vec& color)->bool{
  Vec hsv=colorHSV(color);Canvas panel(320,240);bool result=false,running=true,dirty=true;
  while(running){
   bool apply=false,cancel=false;SDL_Event e;
   while(SDL_PollEvent(&e)){
    if(e.type==SDL_QUIT)cancel=true;
    else if(e.type==SDL_KEYDOWN&&!e.key.repeat){SDL_Scancode s=e.key.keysym.scancode;
     if(s==SDL_SCANCODE_RETURN||s==SDL_SCANCODE_KP_ENTER)apply=true;else if(s==SDL_SCANCODE_ESCAPE||s==SDL_SCANCODE_B||s==SDL_SCANCODE_BACKSPACE)cancel=true;
     else if(s==SDL_SCANCODE_H){Vec c=hsvColor(hsv.x,hsv.y,hsv.z);char hex[8];std::snprintf(hex,sizeof(hex),"%02X%02X%02X",int(c.x*255+.5f),int(c.y*255+.5f),int(c.z*255+.5f));
      std::string text=hex;if(keyboard(title+" HEX RRGGBB",text)){if(!text.empty()&&text[0]=='#')text.erase(0,1);char* end=nullptr;unsigned long value=std::strtoul(text.c_str(),&end,16);
       if(text.size()==6&&text.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos&&*end=='\0'){hsv=colorHSV({float((value>>16)&255)/255,float((value>>8)&255)/255,float(value&255)/255});dirty=true;}}}
    }else if(e.type==SDL_MOUSEBUTTONDOWN&&e.button.button==SDL_BUTTON_LEFT){int tx,ty;
     if(toPanel(e.button.x,e.button.y,tx,ty)&&ty>=222){apply=tx>=176;cancel=tx<=142;}}
    else if(e.type==SDL_MOUSEWHEEL){hsv.z=clamp(hsv.z+e.wheel.y*.03f,0,1);dirty=true;}
   }
   int mx,my;if(SDL_GetMouseState(&mx,&my)&SDL_BUTTON_LMASK){int tx,ty;
    if(toPanel(mx,my,tx,ty)){float dx=float(tx-92),dy=float(ty-116),r=std::sqrt(dx*dx+dy*dy);
     if(r<=84){hsv.x=std::atan2(dy,dx)/6.2831853f;hsv.x-=std::floor(hsv.x);hsv.y=clamp(r/80,0,1);dirty=true;}
     else if(tx>=188&&tx<=234&&ty>=40&&ty<=185){hsv.z=clamp(1-float(ty-45)/135,0,1);dirty=true;}}}
   const Uint8* k=SDL_GetKeyboardState(nullptr);
   float dh=float(k[SDL_SCANCODE_L]||k[SDL_SCANCODE_RIGHT])-float(k[SDL_SCANCODE_J]||k[SDL_SCANCODE_LEFT]);
   float ds=float(k[SDL_SCANCODE_I]||k[SDL_SCANCODE_UP])-float(k[SDL_SCANCODE_K]||k[SDL_SCANCODE_DOWN]);
   float dv=float(k[SDL_SCANCODE_E])-float(k[SDL_SCANCODE_Q]);
   if(dh||ds||dv){hsv.x+=dh*.005f;hsv.x-=std::floor(hsv.x);hsv.y=clamp(hsv.y+ds*.01f,0,1);hsv.z=clamp(hsv.z+dv*.01f,0,1);dirty=true;}
   if(cancel)running=false;
   else if(apply){color=hsvColor(hsv.x,hsv.y,hsv.z);result=true;running=false;}
   if(dirty){drawColorPicker(panel,hsv.x,hsv.y,hsv.z);dirty=false;}
   present(panel);SDL_Delay(16);
  }
  pf.absorb();return result;
 };

 // Exports one screenshot set like the 3DS START button: JPEG of the render, a 3D MPO when stereo is active,
 // and a JPEG of render + panel.
 auto takeScreenshot=[&](bool activeStereo){
  std::filesystem::create_directories(dir+"/screenshots");
  std::vector<Color> top,right;int w=0,h=0,w2=0,h2=0;
  bool captured=currentImage(0,top,w,h);
  if(captured&&activeStereo){captured=currentImage(1,right,w2,h2)&&w2==w&&h2==h;}
  if(!captured){status="SCREENSHOT READBACK FAILED";return;}
  std::string base=dir+"/screenshots/shot-"+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  bool saved=saveJPEG(top,w,h,base+".jpg",status);
  bool mpo=activeStereo&&w==W&&h==H;
  if(saved&&mpo)saved=saveMPO(top,right,base+".mpo",status);
  if(saved&&h*2<=16384){std::vector<Color> both(size_t(w)*h*2,Color{});std::copy(top.begin(),top.end(),both.begin());
   int x0=std::max(0,(w-320)/2);
   for(int y=0;y<std::min(h,240);++y)for(int x=0;x<320&&x0+x<w;++x)both[size_t(y+h)*w+x0+x]=bottom.pixels[size_t(y)*320+x];
   saved=saveJPEG(both,w,h*2,base+"-screens.jpg",status);}
  if(saved)status=activeStereo?(mpo?"JPEG + 3D MPO SAVED":"JPEG SAVED / MPO NEEDS FULL RES"):"JPEG SCREENSHOT SAVED";
 };

 std::vector<Field> fields;int menuTab=-1;uint64_t nextMenuRefresh=0;
 bool panelRefresh=false;uint64_t panelRevision=1;float panelWorkMs=-1;double fpsMs=16;
 renderer.invalidate(scene,false);
 uint64_t previous=uint64_t(nowMs()),nextRepeat=0;uint32_t lastDirection=0;int lastMouseX=-1,lastMouseY=-1;
 Input in;int exitCode=0;
 while(true){
  uint64_t now=uint64_t(nowMs());float dt=clamp(float(now-previous)*.001f,.001f,.05f);previous=now;
  double frameStart=nowMs();
  if(headless&&args.wheel&&frameNumber==2){SDL_Event e{};e.type=SDL_MOUSEWHEEL;e.wheel.y=args.wheel;SDL_PushEvent(&e);}
  pf.poll(in);
  if(args.move>0&&int(frameNumber)<args.move)in.circleY=1;
  if(in.quit||(in.down&KEY_START))break;
  if(in.togglePanel){panelVisible=!panelVisible;}
  if(in.screenshot)screenshotRequested=true;
  if(in.fullscreen){pf.setFullscreen(!pf.fullscreen);menuDirty=true;}
  uint32_t down=in.down,held=in.held;
  changed=false;
  if(in.strengthStep){stereoStrength=clamp(stereoStrength+in.strengthStep/16.f,0,1);menuDirty=true;status="STEREO STRENGTH "+number(stereoStrength);}
  Layout lay=layout();
  int touchX=0,touchY=0;
  if(down&KEY_TOUCH){if(!toPanel(in.clickX,in.clickY,touchX,touchY))down&=~KEY_TOUCH;}
  int hoverX,hoverY;bool overPanel=toPanel(in.mouseX,in.mouseY,hoverX,hoverY);
  bool mouseMoved=in.mouseX!=lastMouseX||in.mouseY!=lastMouseY;lastMouseX=in.mouseX;lastMouseY=in.mouseY;
  if(down&KEY_SELECT){tab=(tab+1)%6;selected=0;}
  if(down&KEY_Y){scene.settings.quality=!scene.settings.quality;changed=true;status=scene.settings.quality?"QUALITY: FULL RES / SLOW":"LIVE PREVIEW";}
  if(down&KEY_B){scene.camera=presetIndex>=0?presetCamera(presetIndex):Camera{};changed=true;status="CAMERA RESET";}
  float forward=in.circleY,strafe=in.circleX,yaw=in.stickX,pitch=in.stickY;
  if(held&KEY_L)yaw-=1;
  if(held&KEY_R)yaw+=1;
  float lookYaw=in.lookX*.0035f,lookPitch=-in.lookY*.0035f;
  float vertical=(held&KEY_ZR?1.f:0.f)-(held&KEY_ZL?1.f:0.f);
  bool motion=forward||strafe||yaw||pitch||vertical||lookYaw||lookPitch;
  if(motion){
   // Camera motion exits expensive still mode immediately.
   if(scene.settings.quality){scene.settings.quality=false;changed=true;}
   scene.camera.yaw+=yaw*dt*1.3f+lookYaw;
   if(scene.camera.yaw>3.14159265f)scene.camera.yaw-=6.2831853f;
   if(scene.camera.yaw< -3.14159265f)scene.camera.yaw+=6.2831853f;
   scene.camera.pitch=clamp(scene.camera.pitch+pitch*dt*1.3f+lookPitch,-1.5f,1.5f);
   Rays basis(scene);float speed=scene.camera.speed*cameraSpeedScale(scene)*dt*(held&KEY_X?3:1);
   Vec moved=scene.camera.position+(basis.forward*forward+basis.right*strafe+Vec{0,vertical,0})*speed;
   if(std::isfinite(moved.x)&&std::isfinite(moved.y)&&std::isfinite(moved.z))scene.camera.position=moved;
  }
  // MATCH WINDOW follows the render view once its size has been stable for a moment.
  if(resIndex==MatchWindow){int w=std::max(1,lay.vw),h=std::max(1,lay.vh);
   if(matchW==0){matchW=w;matchH=h;applyPending=true;}
   else if(w!=matchW||h!=matchH){if(matchSince==0)matchSince=nowMs();if(nowMs()-matchSince>300){matchW=w;matchH=h;matchSince=0;applyPending=true;}}
   else matchSince=0;}
  if(menuDirty||changed||menuTab!=tab||(motion&&tab==4)||(tab==5&&now>=nextMenuRefresh)){
  fields.clear();menuDirty=false;menuTab=tab;nextMenuRefresh=now+250;panelRefresh=true;
  bool selectedKindIsMenger=tab==3&&!scene.formula.stages.empty()&&scene.formula.stages[std::min(stageIndex,int(scene.formula.stages.size())-1)].kind==Kind::Menger;
  auto action=[&](std::string label,std::string value,std::function<void()> fn){fields.push_back({{label,value},{},fn});};
  auto real=[&](std::string label,float& v,float step,float lo,float hi){
   // No artificial limits: any finite value is accepted. Only conditions the math needs are enforced
   // (positive divisors / radii, non-negative strengths, FOV in (0,180), pitch within +-1.5 for a stable basis).
   const float Inf=std::numeric_limits<float>::infinity(),Tiny=std::numeric_limits<float>::min();
   static const char* positive[]={"HIT EPSILON","MIN HIT EPSILON","PIXEL TOLERANCE","STEP SAFETY","FAR CLIP","FRAME BUDGET MS","FOCUS DISTANCE","CONVERGENCE",
    "POINT RANGE","INDIRECT RANGE","FIELD GRID SPACING","BAILOUT","TERMINAL RADIUS","DERIVATIVE SCALE","SLOWDOWN DISTANCE","MIN SPEED FRACTION","MOVE SPEED",
    "GI ERROR THRESHOLD","GI DEPTH EDGE","MINIMUM RADIUS","FIXED RADIUS","FOLD X","FOLD Y","FOLD Z","INVERSION RADIUS"};
   static const char* nonnegative[]={"LENS APERTURE","FOG DENSITY","SPECULAR","EXPOSURE","SUN STRENGTH","COLOR EMISSION","BLOOM HALO","POINT INTENSITY","INDIRECT STRENGTH",
    "EYE SEPARATION","REPEAT X SPACING","REPEAT Y SPACING","REPEAT Z SPACING"};
   bool keep=label=="STEREO STRENGTH"||label=="CAMERA PITCH";
   if(!keep){lo=-Inf;hi=Inf;
    for(const char* p:positive)if(label==p)lo=Tiny;
    for(const char* p:nonnegative)if(label==p)lo=0;
    if(label=="FIELD OF VIEW"){lo=.01f;hi=179.99f;}
    else if(label=="POWER")lo=1.0001f;
    else if(label=="PREPASS START FRACTION"){lo=0;hi=.999f;}
    else if(label=="RELAXATION EXP")lo=1;
    else if(label=="POINT RANGE")lo=Tiny;
    else if(label=="SCALE"&&selectedKindIsMenger)lo=1.0001f;}
   fields.push_back({{label,number(v)},[&,step,lo,hi](int d){float n=clamp(v+d*step*adjustScale,lo,hi);if(std::isfinite(n))v=n;changed=true;},
    [&,label,lo,hi](){std::string input=number(v);if(keyboard(label,input)){char* end=nullptr;errno=0;float n=std::strtof(input.c_str(),&end);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&std::isfinite(n)&&n>=lo&&n<=hi){v=n;changed=true;}else status="INVALID NUMBER / RANGE";
    }} });
  };
  auto integer=[&](std::string label,int& v,int step,int lo,int hi,std::function<void()> hook={}){
   bool structural=label=="GRADIENT STOPS"||label=="STAGE INDEX"||label=="EDIT STAGE"||label=="SCENE SLOT"||label=="TERMINAL SHAPE";
   if(!structural)hi=std::numeric_limits<int>::max();
   if(label=="RAY STEPS"||label=="SKY RAY MIN STEPS"||label=="INDIRECT STEPS"||label=="GI MIN PASSES"||label=="TARGET REFRESH FPS")lo=1;
   auto stepped=[](int value,long long delta,int low,int high){long long n=(long long)value+delta;return int(std::max<long long>(low,std::min<long long>(high,n)));};
   fields.push_back({{label,std::to_string(v)},[&,step,lo,hi,hook,stepped](int d){long long mult=adjustScale>=1?(long long)(adjustScale+.5f):1;v=stepped(v,(long long)d*step*mult,lo,hi);changed=true;if(hook)hook();},
    [&,label,lo,hi,hook](){std::string input=std::to_string(v);if(keyboard(label,input)){char* end=nullptr;errno=0;long long n=std::strtoll(input.c_str(),&end,10);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&n>=lo&&n<=hi){v=int(n);changed=true;if(hook)hook();}else status="INVALID INTEGER / RANGE";
    }} });
  };
  // Block sizes: any integer >= 1 (typed), D-pad / wheel halves or doubles. Capped at 2^20 only to keep index math in int range.
  auto blocks=[&](std::string label,int& v){
   auto set=[&,label](int n){v=std::max(1,std::min(1<<20,n));changed=true;};
   fields.push_back({{label,std::to_string(v)},[&,set](int d){set(d>0?v*2:v/2);},
    [&,label,set](){std::string input=std::to_string(v);if(keyboard(label,input)){char* end=nullptr;errno=0;long n=std::strtol(input.c_str(),&end,10);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&n>=1&&n<=(1<<20))set(int(n));else status="INVALID INTEGER / RANGE";}}});
  };
  auto boolean=[&](std::string label,bool& v){auto fn=[&](){v=!v;changed=true;};fields.push_back({{label,v?"ON":"OFF"},[fn](int){fn();},fn});};
  // PC-only rows: cycle through named values; `scene` is untouched unless render parameters change.
  auto cycle=[&](std::string label,std::string value,std::function<void(int)> fn){fields.push_back({{label,value},[&,fn](int d){fn(d);menuDirty=true;},[&,fn](){fn(1);menuDirty=true;}});};
  if(tab==0){for(int i=0;i<PresetCount;++i)action(presetName(i),presetIndex==i?"CURRENT":"A LOAD",[&,i](){loadPreset(i);changed=true;status="PRESET LOADED";});}
  else if(tab==1){Settings& v=scene.settings;
   boolean("QUALITY MODE",v.quality);blocks("PREVIEW BLOCK",v.previewBlock);blocks("STILL BLOCK",v.stillBlock);
   const char* scaling[]={"NEAREST","BILINEAR","BICUBIC"};fields.push_back({{"UPSCALING",scaling[v.upscale]},[&](int d){v.upscale=(v.upscale+d+3)%3;changed=true;},{}});integer("INTERLACE LANES",v.interlace,1,1,8);
   integer("RAY STEPS",v.steps,8,1,1);real("HIT EPSILON",v.epsilon,.0005f,.000001f,.05f);boolean("ZOOM PRECISION EXP",v.adaptivePrecision);real("MIN HIT EPSILON",v.minEpsilon,.000001f,.000001f,.001f);real("PIXEL TOLERANCE",v.pixelTolerance,.05f,.05f,2);real("STEP SAFETY",v.safety,.05f,.05f,1);
   real("FAR CLIP",v.farClip,1,1,100000);real("FRAME BUDGET MS",v.budgetMs,1,1,100);boolean("AUTO REFINE",v.autoRefine);boolean("ADAPT DETAIL EXP",v.adaptiveDetail);integer("MOVE ITERATIONS",v.previewIterations,1,1,256);integer("DETAIL ITERATION CAP",v.detailIterations,1,1,256);
   boolean("DEPTH OF FIELD",v.dof);real("LENS APERTURE",v.aperture,.01f,0,100);real("FOCUS DISTANCE",v.focusDistance,.1f,.001f,10000);integer("LENS SAMPLES",v.dofSamples,1,1,1);
   action("FOCUS AT CONVERGENCE","A MATCH DISTANCE",[&](){v.focusDistance=v.convergence;changed=true;});
   integer("QUALITY SAMPLES",v.samples,1,1,1);integer("AO SAMPLES",v.ao,1,0,64);integer("SHADOW STEPS",v.shadow,4,0,4096);
   boolean("STEREO",v.stereo);real("EYE SEPARATION",v.eyeSeparation,.005f,0,2);real("CONVERGENCE",v.convergence,.2f,.2f,10000);
   boolean("PARALLEL BATCHES",v.parallel);
   integer("BATCH SIZE",v.batchSize,1,1,32);boolean("ADAPT RESOLUTION",v.adaptiveResolution);integer("TARGET REFRESH FPS",v.targetFps,5,5,240);
   boolean("FAST MOVE LIGHTING",v.previewLighting);boolean("DISTANCE FIELD EXP",v.distanceField);real("FIELD GRID SPACING",v.fieldSpacing,.05f,.05f,1);boolean("TEMPORAL EXPERIMENT",v.temporal);boolean("STEREO REUSE EXP",v.stereoReuse);
   boolean("DEPTH PREPASS",v.depthPrepass);blocks("PREPASS BLOCK",v.prepassBlock);real("PREPASS START FRACTION",v.prepassSafety,.05f,0,.9f);boolean("ADAPT RAY BUDGET",v.adaptiveRayBudget);integer("SKY RAY MIN STEPS",v.rayMinSteps,4,4,4096);boolean("RETRY SHORT RAYS",v.rayBudgetRetry);
   boolean("ADAPT EMPTY SPACE",v.adaptiveEmpty);boolean("ADAPT SKY EDGES",v.adaptiveSkyEdges);integer("SKY PROBE EVERY N",v.emptyProbe,1,1,64);integer("EDGE PROBE EVERY N",v.skyEdgeProbe,1,1,64);integer("SKY NEIGHBORS NEEDED",v.skyNeighbors,1,1,9);integer("DENSE REFRESH PASSES",v.emptyRefresh,1,0,256);
   boolean("ADAPTIVE TILES EXP",v.adaptiveTiles);boolean("FIXED FOVEATION EXP",v.foveated);real("RELAXATION EXP",v.relaxation,.05f,1,1.5f);
   integer("HISTORY FRAMES",v.historyFrames,1,1,30);integer("REFRESH EVERY N",v.refreshRate,1,1,8);
   real("FIELD OF VIEW",v.fov,5,5,170);real("MOVE SPEED",scene.camera.speed,.1f,.01f,1000);
   boolean("SURFACE SLOWDOWN",scene.camera.surfaceSpeed);real("SLOWDOWN DISTANCE",scene.camera.surfaceRange,.1f,.001f,20);real("MIN SPEED FRACTION",scene.camera.minimumSpeed,.005f,.0001f,1);
   // DISPLAY section (PC additions).
   cycle("BACKEND",wantGpu?(useGpu?"GPU":"GPU>CPU"):"CPU",[&](int){wantGpu=!wantGpu;applyPending=true;changed=true;status=wantGpu?"BACKEND GPU":"BACKEND CPU";});
   cycle("RENDER RESOLUTION",resolutions[resIndex].name,[&](int d){resIndex=(resIndex+d+ResolutionCount)%ResolutionCount;if(resIndex==MatchWindow)matchW=0;applyPending=true;changed=true;});
   integer("CUSTOM WIDTH",customW,64,1,1,[&](){resIndex=CustomResolution;applyPending=true;});
   integer("CUSTOM HEIGHT",customH,36,1,1,[&](){resIndex=CustomResolution;applyPending=true;});
   cycle("MOTION SCALE",motionDiv==1?"1":"1/"+std::to_string(motionDiv),[&](int d){motionDiv=d>0?(motionDiv>=4?1:motionDiv*2):(motionDiv<=1?4:motionDiv/2);changed=true;});
   cycle("FULLSCREEN",pf.fullscreen?"ON":"OFF",[&](int){pf.setFullscreen(!pf.fullscreen);});
   cycle("SCALING FILTER",linear?"LINEAR":"NEAREST",[&](int){linear=!linear;});
   cycle("STEREO MODE",stereoModeNames[stereoMode],[&](int d){stereoMode=(stereoMode+d+3)%3;});
   real("STEREO STRENGTH",stereoStrength,.0625f,0,1);
   {int ww,wh;SDL_GetWindowSize(pf.window,&ww,&wh);cycle("WINDOW SIZE",std::to_string(ww)+"X"+std::to_string(wh),[&](int d){
     int best=0;SDL_Window* w=pf.window;int cw,ch;SDL_GetWindowSize(w,&cw,&ch);for(int i=0;i<4;++i)if(windowSizes[i][0]<=cw)best=i;
     best=(best+d+4)%4;if(pf.fullscreen)pf.setFullscreen(false);SDL_SetWindowSize(w,windowSizes[best][0],windowSizes[best][1]);});}
   cycle("PANEL SCALE",panelScale?std::to_string(panelScale)+"X":"AUTO",[&](int d){panelScale=(panelScale+d+5)%5;});
  }else if(tab==2){Formula& f=scene.formula;
   boolean("ALGEBRAIC BULB EXP",f.algebraicBulb);
   boolean("WORLD REPEAT",f.repeat);real("REPEAT X SPACING",f.repeatPeriod.x,1,0,100000);real("REPEAT Y SPACING",f.repeatPeriod.y,1,0,100000);real("REPEAT Z SPACING",f.repeatPeriod.z,1,0,100000);
   integer("ITERATIONS",f.iterations,1,1,256);real("BAILOUT",f.bailout,1,2,1000000);boolean("LOG DISTANCE",f.logarithmic);
   integer("TERMINAL SHAPE",f.terminal,1,0,3);real("TERMINAL RADIUS",f.terminalRadius,.1f,.000001f,10000);
   real("DERIVATIVE SCALE",f.derivativeScale,.25f,.001f,10000);boolean("JULIA MODE",f.julia);
   real("JULIA X",f.constant.x,.05f,-100,100);real("JULIA Y",f.constant.y,.05f,-100,100);real("JULIA Z",f.constant.z,.05f,-100,100);
   integer("EDIT STAGE",stageIndex,1,0,int(f.stages.size())-1);
   action("OPEN STAGE EDITOR","A OPEN",[&](){tab=3;selected=0;});
   action("ADD STAGE","A ADD OFFSET",[&](){f.stages.push_back(newStage(Kind::Offset));stageIndex=int(f.stages.size())-1;changed=true;});
  }else if(tab==3){Formula& f=scene.formula;stageIndex=std::min(stageIndex,int(f.stages.size())-1);Stage& a=f.stages[stageIndex];
   integer("STAGE INDEX",stageIndex,1,0,int(f.stages.size())-1);
   fields.push_back({{"OPERATION",kindName(a.kind)},[&](int d){int k=(int(a.kind)+d+int(Kind::Count))%int(Kind::Count);a=newStage(Kind(k));changed=true;},{}});
   boolean("ENABLED",a.enabled);
   const char* labels[3]={"PARAMETER A","PARAMETER B","PARAMETER C"};
   if(a.kind==Kind::KleinianFold){labels[0]="FOLD X";labels[1]="FOLD Y";labels[2]="FOLD Z";}
   if(a.kind==Kind::Inversion)labels[0]="INVERSION RADIUS";
   if(a.kind==Kind::BoxFold)labels[0]="FOLD LIMIT";
   if(a.kind==Kind::SphereFold){labels[0]="MINIMUM RADIUS";labels[1]="FIXED RADIUS";}
   if(a.kind==Kind::Bulb){labels[0]="POWER";labels[1]="THETA MULTIPLIER";labels[2]="PHI MULTIPLIER";}
   if(a.kind==Kind::Scale){labels[0]="SCALE";labels[1]="C MULTIPLIER";}
   if(a.kind==Kind::Rotate){labels[0]="X RADIANS";labels[1]="Y RADIANS";labels[2]="Z RADIANS";}
   if(a.kind==Kind::Offset){labels[0]="X OFFSET";labels[1]="Y OFFSET";labels[2]="Z OFFSET";}
   if(a.kind==Kind::Menger){labels[0]="SCALE";labels[1]="XY OFFSET";labels[2]="Z OFFSET";}
   if(a.kind==Kind::Expression){
    for(int i=0;i<3;++i)action(std::string(1,"XYZ"[i])+" EXPRESSION",a.text[i],[&,i](){std::string value=a.text[i];
     if(keyboard(std::string(1,"XYZ"[i])+" = f(x,y,z,cx,cy,cz)",value)){Expression e;std::string error;if(e.compile(value,error)){a.text[i]=value;changed=true;}else status=error;}
    });
   }else if(a.kind!=Kind::Absolute&&a.kind!=Kind::Sort&&a.kind!=Kind::Tetra){
    real(labels[0],a.a,.05f,a.kind==Kind::Bulb||a.kind==Kind::Menger?1.1f:a.kind==Kind::SphereFold?.001f:-1000,a.kind==Kind::Bulb?64:1000);
    if(a.kind!=Kind::BoxFold&&a.kind!=Kind::Inversion){real(labels[1],a.b,.05f,a.kind==Kind::SphereFold?.001f:a.kind==Kind::Bulb?-16:-1000,a.kind==Kind::Bulb?16:1000);
     if(a.kind!=Kind::SphereFold&&a.kind!=Kind::Scale)real(labels[2],a.c,.05f,a.kind==Kind::Bulb?-16:-1000,a.kind==Kind::Bulb?16:1000);
    }
   }
   action("MOVE EARLIER","A REORDER",[&](){if(stageIndex>0){std::swap(f.stages[stageIndex],f.stages[stageIndex-1]);--stageIndex;changed=true;}});
   action("MOVE LATER","A REORDER",[&](){if(stageIndex+1<int(f.stages.size())){std::swap(f.stages[stageIndex],f.stages[stageIndex+1]);++stageIndex;changed=true;}});
   action("DUPLICATE STAGE","A COPY",[&](){Stage copy=a;f.stages.insert(f.stages.begin()+stageIndex+1,copy);++stageIndex;changed=true;});
   action("DELETE STAGE","A REMOVE",[&](){if(f.stages.size()>1){f.stages.erase(f.stages.begin()+stageIndex);stageIndex=std::min(stageIndex,int(f.stages.size())-1);changed=true;}else status="KEEP AT LEAST ONE STAGE";});
  }else if(tab==4){Settings& v=scene.settings;
   const char* names[]={"RUST","SEA GLASS","NEON ROSE","BONE"};
   auto cyclePalette=[&](int d){v.palette=float((int(v.palette)+d+4)%4);v.customGradient=false;changed=true;};
   fields.push_back({{"PALETTE",names[int(v.palette)%4]},cyclePalette,[cyclePalette](){cyclePalette(1);}});
   boolean("CUSTOM GRADIENT",v.customGradient);
   real("COLOR EMISSION",v.emission,.1f,0,4);real("BLOOM HALO",v.bloom,.1f,0,4);
   auto colorField=[&](std::string label,Vec& c){char hex[8];std::snprintf(hex,sizeof(hex),"%02X%02X%02X",int(clamp(c.x,0,1)*255+.5f),int(clamp(c.y,0,1)*255+.5f),int(clamp(c.z,0,1)*255+.5f));
    action(label,hex,[&,label](){if(pickColor(label,c)){if(label.rfind("GRADIENT ",0)==0)v.customGradient=true;changed=true;}menuDirty=true;});
   };
   boolean("BOUNDED COLOR MAP",v.boundedGradient);integer("GRADIENT STOPS",v.gradientStops,1,2,5);
   colorField("GRADIENT START",v.gradientLow);
   for(int i=0;i<v.gradientStops-2;++i)colorField("GRADIENT STOP "+std::to_string(i+2),v.gradientMiddle[i]);
   colorField("GRADIENT END",v.gradientHigh);
   action("AUTO FIT GRADIENT","A FIT CURRENT VIEW",[&](){if(useGpu)status="AUTO FIT NEEDS CPU BACKEND";else if(renderer.fitGradient(scene)){changed=true;status="GRADIENT FIT TO VISIBLE SURFACE";}else status="FINISH PINHOLE RENDER / NO VARIATION";});
   real("GRADIENT SCALE",v.gradientScale,.1f,.01f,100);real("GRADIENT OFFSET",v.gradientOffset,.05f,-100,100);boolean("REPEAT GRADIENT",v.gradientRepeat);
   real("LIGHT YAW",v.lightYaw,.1f,-6.3f,6.3f);real("LIGHT PITCH",v.lightPitch,.1f,-1.5f,1.5f);real("EXPOSURE",v.exposure,.1f,.1f,16);
   real("SUN STRENGTH",v.sunStrength,.1f,0,4);
   fields.push_back({{"EDIT POINT LIGHT",std::to_string(lightIndex+1)},[&](int d){lightIndex=(lightIndex+d+2)%2;menuDirty=true;},{}});
   PointLight& lamp=v.pointLights[lightIndex];boolean("POINT LIGHT ENABLED",lamp.enabled);boolean("CAMERA RELATIVE LIGHT",lamp.cameraRelative);
   real("POINT X",lamp.position.x,.1f,-1e6f,1e6f);real("POINT Y",lamp.position.y,.1f,-1e6f,1e6f);real("POINT Z",lamp.position.z,.1f,-1e6f,1e6f);
   colorField("POINT COLOR",lamp.color);real("POINT INTENSITY",lamp.intensity,.5f,0,10000);real("POINT RANGE",lamp.range,.5f,.1f,10000);boolean("POINT SHADOWS",v.pointShadows);
   auto progressive=[&](){v.progressiveLighting=!v.progressiveLighting;if(v.progressiveLighting&&v.giSamples==0)v.giSamples=1;changed=true;};
   fields.push_back({{"PROGRESSIVE LIGHTING",v.progressiveLighting?"ON":"OFF"},[progressive](int){progressive();},progressive});
   boolean("SEPARATE GI RES",v.separateLighting);boolean("ADAPT GI SAMPLES",v.adaptiveLighting);integer("GI MIN PASSES",v.lightingMinPasses,1,2,4096);real("GI ERROR THRESHOLD",v.lightingThreshold,.005f,.0001f,1);integer("GI REFRESH PASSES",v.lightingRefresh,1,0,256);real("GI DEPTH EDGE",v.lightingDepthTolerance,.01f,.001f,1);
   integer("LIGHTING PASSES",v.lightingPasses,1,1,4096);blocks("LIGHTING BLOCK",v.lightingBlock);
   integer("INDIRECT SAMPLES",v.giSamples,1,0,64);integer("INDIRECT STEPS",v.giSteps,4,4,4096);
   real("INDIRECT STRENGTH",v.giStrength,.1f,0,20);real("INDIRECT RANGE",v.giRange,.1f,.1f,10000);colorField("SKY COLOR",v.skyColor);
   real("SPECULAR",v.specular,.05f,0,1);real("FOG DENSITY",v.fog,.01f,0,1);
   real("CAMERA X",scene.camera.position.x,.1f,-1e6f,1e6f);real("CAMERA Y",scene.camera.position.y,.1f,-1e6f,1e6f);real("CAMERA Z",scene.camera.position.z,.1f,-1e6f,1e6f);
   real("CAMERA YAW",scene.camera.yaw,.1f,-6.3f,6.3f);real("CAMERA PITCH",scene.camera.pitch,.1f,-1.5f,1.5f);
  }else{
   action("SAVE SCREENSHOT","F12 / A JPEG-MPO",[&](){screenshotRequested=true;});
   action("EXIT APP","A QUIT",[&](){exitRequested=true;});
   integer("SCENE SLOT",slot,1,0,7);
   auto path=[&](){return dir+"/scene-"+std::to_string(slot)+".rfs";};
   action("SAVE SCENE","A SAVE / REPLACE",[&,path](){saveScene(scene,path(),status);});
   action("LOAD SCENE","A LOAD",[&,path](){if(loadScene(scene,path(),status)){changed=true;stageIndex=0;presetIndex=-1;}});
   action("EXPORT STEREO PPM","A SAVE / REPLACE",[&](){
    std::string base=dir+"/image-"+std::to_string(slot);std::vector<Color> left,right;int w=0,h=0,w2=0,h2=0;
    if(!currentImage(0,left,w,h)||!currentImage(1,right,w2,h2)){status="READBACK FAILED";return;}
    bool done=useGpu?gpuDone:renderer.complete();
    if(writePPM(left,w,h,base+"-left.ppm",status)&&writePPM(right,w2,h2,base+"-right.ppm",status))status=done?"STEREO PAIR EXPORTED":"PARTIAL FRAME EXPORTED";
   });
   action("SCREENSHOT PNG","A SAVE / REPLACE",[&](){std::vector<Color> img;int w=0,h=0;
    if(currentImage(0,img,w,h)&&savePNG(img,w,h,dir+"/image-"+std::to_string(slot)+".png",status))status="PNG SAVED SLOT "+std::to_string(slot);});
   action("QUALITY RENDER","A START",[&](){scene.settings.quality=true;changed=true;status="QUALITY RENDER STARTED";});
   action("RENDER AGAIN","A RESTART",[&](){changed=true;});
   action("FIELD CACHE READY",distanceField?std::to_string(int(distanceField->progress()*100))+"%":"OFF",[](){});
   action("BENCH CURRENT VIEW","A 1500 MONO SAMPLES",[&](){benchmark=std::make_unique<TraceBenchmark>(scene);benchmarkActive=true;benchmarkStart=nowMs();status="BENCHMARK RUNNING / FROZEN MONO VIEW";});
   action("BENCH MOVE PREVIEW","A 1500 MONO SAMPLES",[&](){benchmark=std::make_unique<TraceBenchmark>(scene,distanceField&&distanceField->ready()?distanceField:nullptr,true);benchmarkActive=true;benchmarkStart=nowMs();status="BENCHMARK / MOVE PREVIEW SNAPSHOT";});
   action("BENCH SAMPLES",benchmark?std::to_string(benchmark->completedSamples())+" / 1500":"NOT RUN",[](){});
   action("BENCH RAYS / SECOND",benchmark?number(benchmark->raysPerSecond()):"NOT RUN",[](){});
   action("CANCEL BENCHMARK","A CANCEL",[&](){benchmarkActive=false;});
   action("LAST FRAME MS",!useGpu&&renderer.lastCompletedFrameBlock()?number(renderer.lastCompletedFrameMs()):"NOT COMPLETED",[](){});
   action("LAST FRAME SCALE / EYES",std::to_string(renderer.lastCompletedFrameBlock())+"X / "+std::to_string(renderer.lastCompletedFrameEyes()),[](){});
   action("LAST REALTIME FRAME MS",!useGpu&&renderer.lastRealtimeFrameBlock()?number(renderer.lastRealtimeFrameMs()):"NOT COMPLETED",[](){});
   action("LAST REALTIME SCALE",std::to_string(renderer.lastRealtimeFrameBlock())+"X",[](){});
   action("UI FRAME / TRACE MS",number(useGpu?float(fpsMs):renderer.measuredFrameMs())+" / "+number(workMs),[](){});
   action("RENDER SIZE / FPS",std::to_string(W)+"X"+std::to_string(H)+" / "+number(float(1000/std::max(1.0,fpsMs))),[](){});
   action("LIGHTING PASSES DONE",std::to_string(renderer.accumulatedPasses())+" / "+std::to_string(scene.settings.lightingPasses),[](){});
   action("RAYS / DE QUERIES",std::to_string(renderer.profile().rays)+" / "+std::to_string(renderer.profile().distanceQueries),[](){});
   action("GI CELLS SKIPPED",std::to_string(renderer.profile().lightingSkipped),[](){});
   action("DEPTH STARTS / RAY RETRIES",std::to_string(renderer.profile().depthStarts)+" / "+std::to_string(renderer.profile().budgetRetries),[](){});
   action("SKY CELLS SKIPPED",std::to_string(renderer.profile().skySkipped),[](){});
   action("REUSE EVENTS / BATCHES",std::to_string(renderer.profile().reused)+" / "+std::to_string(renderer.profile().batches),[](){});
   action("RESET COUNTERS","A RESET",[&](){renderer.resetProfile();});
   action("FILES FOLDER",dir.size()>23?dir.substr(dir.size()-23):dir,[](){});
   action("CONTROLS","A HELP",[&](){status="WASD MOVE ARROWS LOOK Q/E TURN Z/C DN/UP";});
  }
  }
  selected=std::max(0,std::min(selected,int(fields.size())-1));
  uint32_t direction=held&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT);
  uint32_t repeat=down&direction;
  if(direction!=lastDirection){nextRepeat=now+300;lastDirection=direction;}
  else if(direction&&now>=nextRepeat){repeat=direction;nextRepeat=now+90;}
  // Hovering a row selects it (same hit-test as touch); the wheel then scrubs the selected value.
  if(overPanel&&mouseMoved&&hoverY>=70&&hoverY<190){int row=(hoverY-70)/15+(selected/8)*8;if(row<int(fields.size()))selected=row;}
  if(repeat&KEY_DUP)selected=(selected+int(fields.size())-1)%int(fields.size());
  if(repeat&KEY_DDOWN)selected=(selected+1)%int(fields.size());
  std::unique_ptr<Scene> before;int beforeStage=stageIndex;
  bool activate=down&KEY_A;
  if(down&KEY_TOUCH){
   if(touchY>=36&&touchY<53){tab=std::min(5,std::max(0,(touchX-6)/52));selected=0;}
   else if(touchY>=70&&touchY<190){int row=touchY-70;row=row/15+(selected/8)*8;
    if(row<int(fields.size())){selected=row;if(touchX<45)repeat|=KEY_DLEFT;else if(touchX>280)repeat|=KEY_DRIGHT;else activate=true;}
   }
  }
  int wheelSteps=(overPanel||(headless&&args.wheel))?int(std::lround(in.wheel)):0;
  bool materialEditOnly=!changed;
  if((repeat&(KEY_DLEFT|KEY_DRIGHT))||activate||wheelSteps){before.reset(new Scene(scene));menuDirty=true;}
  if((repeat&KEY_DLEFT)&&fields[selected].adjust)fields[selected].adjust(-1);
  if((repeat&KEY_DRIGHT)&&fields[selected].adjust)fields[selected].adjust(1);
  if(wheelSteps&&fields[selected].adjust){
   SDL_Keymod mod=SDL_GetModState();adjustScale=(mod&KMOD_SHIFT)?10.f:(mod&KMOD_CTRL)?.1f:1.f;
   for(int n=std::abs(wheelSteps);n>0;--n)fields[selected].adjust(wheelSteps>0?1:-1);
   adjustScale=1;
  }
  if(activate&&fields[selected].activate)fields[selected].activate();
  if(changed){std::string error;if(!scene.formula.validate(error)){if(before)scene=*before;stageIndex=beforeStage;status=error;}}
  if(changed||applyPending){if(changed)forceCpu=false;reconcile();}
  float previousSlider=slider;
  bool sliderChanged=sliderControl.update(scene.settings,stereoMode==StereoOff?0:stereoStrength);slider=sliderControl.strength;
  if(sliderChanged)sliderUntil=now+250;
  // Enabling stereo at zero strength does not alter any camera ray.
  if(changed&&before&&fields[selected].row.label=="STEREO"&&previousSlider==0&&slider==0)changed=false;
  if(applyPending){applyPending=false;applyBackend();changed=true;gpuSeen=false;gpuEye=-1;uploadedStereo=false;menuDirty=true;}
  if(changed&&materialEditOnly&&before&&tab==4&&!motion&&!useGpu&&now>=sliderUntil&&!sliderChanged){
   const std::string& label=fields[selected].row.label;
   bool material=label=="PALETTE"||label=="CUSTOM GRADIENT"||label.rfind("GRADIENT ",0)==0||label=="SKY COLOR"||label=="AUTO FIT GRADIENT"||label=="BOUNDED COLOR MAP"||label=="COLOR EMISSION"||label=="BLOOM HALO"||label=="GRADIENT SCALE"||label=="GRADIENT OFFSET"||label=="REPEAT GRADIENT"||label=="EXPOSURE"||label=="FOG DENSITY";
   if(material&&renderer.recolor(scene)){changed=false;status="COLOR UPDATED / GEOMETRY REUSED";}
  }
  if(changed&&materialEditOnly&&before&&tab==1&&!useGpu&&fields[selected].row.label=="UPSCALING"){
   bool updated=renderer.rescale(scene);changed=false;
   status=updated?"SCALING UPDATED / FRAME REUSED":"SCALING APPLIES AT NEXT FULL PASS";
  }
  bool activeStereo=slider>0;bool moving=motion||now<sliderUntil;
  double workStart=nowMs();uint64_t beforeJobs=renderer.jobs();
  uint64_t beforeRevision=renderer.imageRevision();
  if(useGpu){
   float eyeOffset=activeStereo?scene.settings.eyeSeparation*slider*.5f:0;
   bool need=changed||eyeOffset!=gpuEye||moving||gpuMoving;
   if(need){std::string error;
    if(gpu.begin(scene,eyeOffset,error))gpuSeen=true;
    else if(!gpuSeen){useGpu=false;forceCpu=true;status="GPU SHADER FAILED / CPU BACKEND";std::fprintf(stderr,"GPU shader: %s\n",error.c_str());applyBackend();renderer.invalidate(scene,false);}
    else status=error;
    gpuEye=eyeOffset;gpuDone=false;gpuMoving=moving;}
   if(useGpu&&!gpuDone&&!benchmarkActive){
    int rw=moving?std::max(1,W/motionDiv):W,rh=moving?std::max(1,H/motionDiv):H;
    gpuDone=gpu.render(scene,rw,rh,moving?std::max(4.f,1000.f/scene.settings.targetFps*.8f):scene.settings.budgetMs*2);
    shownW=rw;shownH=rh;
   }
  }
  if(!useGpu){
   Scene* rendering=&scene;
   if(scene.settings.adaptiveDetail){
    if(!detailScene||changed)detailScene=std::make_unique<Scene>(scene);
    detailScene->settings=scene.settings;detailScene->camera=scene.camera;
    detailScene->formula.iterations=renderingIterations(scene,motion||now<sliderUntil);rendering=detailScene.get();
   }
   renderer.beginFrame(*rendering,moving,changed,slider);
   if(scene.settings.distanceField&&!scene.settings.adaptiveDetail&&(!distanceField||changed||!distanceField->containsCamera(scene.camera.position)))distanceField=std::make_shared<DistanceField>(scene);
   if(!scene.settings.distanceField||scene.settings.adaptiveDetail)distanceField.reset();
   Rays rays(*rendering);rays.field=scene.settings.adaptiveDetail?nullptr:distanceField.get();uint64_t start=uint64_t(nowMs());beforeJobs=renderer.jobs();beforeRevision=renderer.imageRevision();
   workStart=nowMs();
   if(distanceField&&!distanceField->ready()&&!benchmarkActive)distanceField->build(scene.settings.parallel?WorkerPool::parallelFor:nullptr,&pool);
   while(!benchmarkActive&&!renderer.complete()){
    renderer.step(*rendering,rays,slider);
    if(float(nowMs()-double(start))>=scene.settings.budgetMs)break;
   }
   if(renderer.imageRevision()!=uploaded[0]||activeStereo!=uploadedStereo){
    display.upload(0,renderer.image(0),W,H);
    if(activeStereo)display.upload(1,renderer.image(1),W,H);
    uploaded[0]=renderer.imageRevision();uploadedStereo=activeStereo;
   }
  }
  if(benchmarkActive){
   double begin=nowMs();
   do{benchmark->step(scene.settings.parallel?WorkerPool::shade:nullptr,&pool);}
   while(!benchmark->complete()&&nowMs()-begin<scene.settings.budgetMs);
   benchmark->recordMs(float(nowMs()-begin));
   if(benchmark->complete()){
    benchmarkActive=false;bool saved=benchmark->save(dir+"/benchmark-"+std::to_string(slot)+".csv");
    status=saved?"BENCHMARK DONE / CSV SAVED":"BENCHMARK DONE / CSV WRITE FAILED";menuDirty=true;
   }
  }
  workMs=float(nowMs()-workStart);
  bool progressShown=false;(void)progressShown;
  if(panelRefresh||menuDirty||changed||down||repeat||wheelSteps||motion||now<sliderUntil||workMs!=panelWorkMs||renderer.imageRevision()!=beforeRevision||useGpu){
   panelRefresh=false;panelWorkMs=workMs;
   ++panelRevision;std::vector<Row> rows;rows.reserve(fields.size());for(const Field& f:fields)rows.push_back(f.row);
   int block=useGpu?(moving?motionDiv:1):renderer.currentBlock();float progress=useGpu?(gpuDone?1.f:.5f):renderer.progress(scene);
   drawPanel(bottom,tab,selected,rows,status,presetIndex<0?"CUSTOM SCENE":presetName(presetIndex),block,progress,scene.settings.quality,workMs);
   if(tab==4){Settings gradient=scene.settings;gradient.gradientScale=1;gradient.gradientOffset=0;gradient.gradientRepeat=false;gradient.boundedGradient=false;
    for(int x=0;x<300;++x){Vec c=gradientColor(gradient,float(x)/299);bottom.rect(10+x,67,1,3,{uint8_t(c.x*255),uint8_t(c.y*255),uint8_t(c.z*255)});}}
  }
  if(screenshotRequested){screenshotRequested=false;takeScreenshot(activeStereo);menuDirty=true;}
  present(bottom);
  ++frameNumber;
  wantWindowShot=!args.windowShot.empty()&&headless&&int(frameNumber)+1>=args.frames;
  fpsMs=fpsMs*.9+(nowMs()-frameStart)*.1;
  if(!useGpu)renderer.endFrame(scene,float(nowMs()-frameStart),workMs,renderer.jobs()-beforeJobs);
  if(exitRequested)break;
  if(headless&&int(frameNumber)>=args.frames&&(useGpu?gpuDone:renderer.complete())&&!motion&&!benchmarkActive)break;
  if(headless&&frameNumber>40000){std::fprintf(stderr,"selftest: render did not complete\n");exitCode=3;break;}
  if(headless&&nowMs()>180000){std::fprintf(stderr,"selftest: timeout\n");exitCode=3;break;}
 }
 if(headless&&exitCode==0){
  std::vector<Color> img;int w=0,h=0;std::string error;
  if(!currentImage(0,img,w,h)||img.empty()){std::fprintf(stderr,"selftest: no image\n");exitCode=2;}
  else{
   size_t distinct=0;Color first=img[0];for(const Color& c:img)if(c.r!=first.r||c.g!=first.g||c.b!=first.b){++distinct;}
   std::printf("selftest: backend=%s %dx%d frames=%llu varying-pixels=%zu\n",useGpu?"gpu":"cpu",w,h,(unsigned long long)frameNumber,distinct);
   if(args.wheel&&!fields.empty())std::printf("selftest: wheel %d on row %s = %s\n",args.wheel,fields[selected].row.label.c_str(),fields[selected].row.value.c_str());
   if(distinct==0){std::fprintf(stderr,"selftest: image is uniform\n");exitCode=2;}
   if(!args.screenshot.empty()&&!writePPM(img,w,h,args.screenshot,error)){std::fprintf(stderr,"%s\n",error.c_str());exitCode=2;}
   if(!args.panel.empty()&&!writePPM(bottom.pixels,320,240,args.panel,error)){std::fprintf(stderr,"%s\n",error.c_str());exitCode=2;}
  }
 }
 pool.shutdown();gpu.shutdown();display=Display();pf.shutdown();return exitCode;
}
