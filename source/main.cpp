#include <3ds.h>
#include "engine.hpp"
#include "ui.hpp"
#include "stereo_worker.hpp"
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <memory>
#include <sys/stat.h>

using namespace rf;
namespace {
struct Field {Row row;std::function<void(int)> adjust;std::function<void()> activate;};
std::string number(float v){char b[40];std::snprintf(b,sizeof(b),"%.5G",v);return b;}
bool keyboard(const std::string& prompt,std::string& value){
 SwkbdState k;swkbdInit(&k,SWKBD_TYPE_NORMAL,2,256);
 swkbdSetHintText(&k,prompt.c_str());swkbdSetInitialText(&k,value.c_str());
 swkbdSetValidation(&k,SWKBD_NOTEMPTY_NOTBLANK,0,0);
 char buffer[1025]={};if(swkbdInputText(&k,buffer,sizeof(buffer))!=SWKBD_BUTTON_CONFIRM)return false;
 value=buffer;return true;
}
void blit(u8* frame,const std::vector<Color>& pixels,int width){
 // Landscape (x,y) -> rotated, column-major BGR8 framebuffer.
 for(int x=0;x<width;++x)for(int y=0;y<240;++y){const Color& c=pixels[y*width+x];size_t i=(x*240+239-y)*3;
  frame[i]=c.b;frame[i+1]=c.g;frame[i+2]=c.r;
 }
}
struct FrameCopyCache {
 u8* addresses[2]={nullptr,nullptr};uint64_t revisions[2]={0,0};bool valid[2]={false,false};
 void copy(u8* address,const std::vector<Color>& pixels,uint64_t revision){
  int slot=address==addresses[0]?0:address==addresses[1]?1:addresses[0]==nullptr?0:1;
  if(!valid[slot]||addresses[slot]!=address||revisions[slot]!=revision){blit(address,pixels,W);addresses[slot]=address;revisions[slot]=revision;valid[slot]=true;}
 }
 void reset(){valid[0]=valid[1]=false;}
};
Stage newStage(Kind kind){Stage s;s.kind=kind;s.a=0;s.b=0;s.c=0;
 if(kind==Kind::BoxFold)s.a=1;
 if(kind==Kind::SphereFold){s.a=.5f;s.b=1;}
 if(kind==Kind::Bulb){s.a=8;s.b=s.c=1;}
 if(kind==Kind::Scale){s.a=2;s.b=1;}
 if(kind==Kind::Menger){s.a=3;s.b=s.c=1;}
 return s;
}
}
int main(){
 gfxInitDefault();gfxSet3D(false);osSetSpeedupEnable(true);
 FrameCopyCache topCopies[2];bool displayStereo=false;
 bool new3ds=false;APT_CheckNew3DS(&new3ds);
 mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/Re-fract",0777);
 auto sceneOwner=std::make_unique<Scene>();auto rendererOwner=std::make_unique<Renderer>();
 Scene& scene=*sceneOwner;Renderer& renderer=*rendererOwner;Canvas bottom(320,240);
 StereoWorker worker(new3ds);
 if(worker.available())renderer.setBatchShader(StereoWorker::shade,&worker);
 int tab=0,selected=0,presetIndex=0,stageIndex=0,slot=0;float slider=0,workMs=0;StereoSlider sliderControl;uint64_t sliderUntil=0;
 std::string status=worker.available()?"NEW 3DS / TWO CPU BATCHES":new3ds?"NEW 3DS / SINGLE CPU FALLBACK":"OLD 3DS / USE 16X PREVIEW";
 if(!new3ds){scene.settings.previewBlock=16;scene.settings.budgetMs=5;}
 renderer.invalidate(scene,false);
 uint64_t previous=osGetTime(),nextRepeat=0;u32 lastDirection=0;
 bool changed=false,menuDirty=true;int menuTab=-1;uint64_t nextMenuRefresh=0;std::vector<Field> fields;
 while(aptMainLoop()){
  uint64_t now=osGetTime();float dt=clamp(float(now-previous)*.001f,.001f,.05f);previous=now;
  hidScanInput();u32 down=hidKeysDown(),held=hidKeysHeld();
  if(down&KEY_START)break;
  changed=false;
  if(down&KEY_SELECT){tab=(tab+1)%6;selected=0;}
  if(down&KEY_Y){scene.settings.quality=!scene.settings.quality;changed=true;status=scene.settings.quality?"QUALITY: FULL RES / SLOW":"LIVE PREVIEW";}
  if(down&KEY_B){scene.camera=presetIndex>=0?presetCamera(presetIndex):Camera{};changed=true;status="CAMERA RESET";}
  circlePosition circle,stick;hidCircleRead(&circle);hidCstickRead(&stick);
  auto axis=[](s16 v){return std::abs(v)>15?float(v)/156:0.f;};
  float forward=axis(circle.dy),strafe=axis(circle.dx),yaw=axis(stick.dx),pitch=axis(stick.dy);
  if(held&KEY_L)yaw-=1;
  if(held&KEY_R)yaw+=1;
  float vertical=(held&KEY_ZR?1.f:0.f)-(held&KEY_ZL?1.f:0.f);
  bool motion=forward||strafe||yaw||pitch||vertical;
  if(motion){
   // Camera motion exits expensive still mode immediately.
   if(scene.settings.quality){scene.settings.quality=false;changed=true;}
   scene.camera.yaw+=yaw*dt*1.3f;
   if(scene.camera.yaw>3.14159265f)scene.camera.yaw-=6.2831853f;
   if(scene.camera.yaw< -3.14159265f)scene.camera.yaw+=6.2831853f;
   scene.camera.pitch=clamp(scene.camera.pitch+pitch*dt*1.3f,-1.5f,1.5f);
   Rays basis(scene);float speed=scene.camera.speed*cameraSpeedScale(scene)*dt*(held&KEY_X?3:1);
   scene.camera.position=scene.camera.position+(basis.forward*forward+basis.right*strafe+Vec{0,vertical,0})*speed;
   scene.camera.position.x=clamp(scene.camera.position.x,-10000,10000);
   scene.camera.position.y=clamp(scene.camera.position.y,-10000,10000);
   scene.camera.position.z=clamp(scene.camera.position.z,-10000,10000);
  }
  if(menuDirty||changed||menuTab!=tab||(motion&&tab==4)||(tab==5&&now>=nextMenuRefresh)){
  fields.clear();menuDirty=false;menuTab=tab;nextMenuRefresh=now+250;
  auto action=[&](std::string label,std::string value,std::function<void()> fn){fields.push_back({{label,value},{},fn});};
  auto real=[&](std::string label,float& v,float step,float lo,float hi){
   fields.push_back({{label,number(v)},[&,step,lo,hi](int d){v=clamp(v+d*step,lo,hi);changed=true;},
    [&,label,lo,hi](){std::string input=number(v);if(keyboard(label,input)){char* end=nullptr;errno=0;float n=std::strtof(input.c_str(),&end);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&std::isfinite(n)&&n>=lo&&n<=hi){v=n;changed=true;}else status="INVALID NUMBER / RANGE";
    }} });
  };
  auto integer=[&](std::string label,int& v,int step,int lo,int hi){
   fields.push_back({{label,std::to_string(v)},[&,step,lo,hi](int d){v=std::max(lo,std::min(hi,v+d*step));changed=true;},
    [&,label,lo,hi](){std::string input=std::to_string(v);if(keyboard(label,input)){char* end=nullptr;errno=0;long n=std::strtol(input.c_str(),&end,10);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&n>=lo&&n<=hi){v=int(n);changed=true;}else status="INVALID INTEGER / RANGE";
    }} });
  };
  auto boolean=[&](std::string label,bool& v){auto fn=[&](){v=!v;changed=true;};fields.push_back({{label,v?"ON":"OFF"},[fn](int){fn();},fn});};
  auto choice=[&](std::string label,int& v,const std::vector<int>& values){fields.push_back({{label,std::to_string(v)},
   [&,values](int dir){auto it=std::find(values.begin(),values.end(),v);int i=it==values.end()?0:int(it-values.begin());i=(i+dir+int(values.size()))%int(values.size());v=values[i];changed=true;},{}});};
  if(tab==0){for(int i=0;i<PresetCount;++i)action(presetName(i),presetIndex==i?"CURRENT":"A LOAD",[&,i](){scene.formula=preset(i);presetIndex=i;stageIndex=0;scene.camera=presetCamera(i);scene.settings.farClip=40;scene.settings.convergence=-scene.camera.position.z;changed=true;status="PRESET LOADED";});}
  else if(tab==1){Settings& v=scene.settings;
   boolean("QUALITY MODE",v.quality);choice("PREVIEW BLOCK",v.previewBlock,{4,8,16});integer("INTERLACE LANES",v.interlace,1,1,8);
   integer("RAY STEPS",v.steps,8,8,256);real("HIT EPSILON",v.epsilon,.0005f,.0001f,.05f);real("STEP SAFETY",v.safety,.05f,.05f,1);
   real("FAR CLIP",v.farClip,1,1,100);real("FRAME BUDGET MS",v.budgetMs,1,1,20);boolean("AUTO REFINE",v.autoRefine);
   choice("QUALITY SAMPLES",v.samples,{1,2,4});integer("AO SAMPLES",v.ao,1,0,6);integer("SHADOW STEPS",v.shadow,4,0,64);
   boolean("STEREO",v.stereo);real("EYE SEPARATION",v.eyeSeparation,.005f,0,.2f);real("CONVERGENCE",v.convergence,.2f,.2f,30);
   boolean("PARALLEL BATCHES",v.parallel);
   integer("BATCH SIZE",v.batchSize,1,1,32);boolean("ADAPT RESOLUTION",v.adaptiveResolution);integer("TARGET REFRESH FPS",v.targetFps,5,15,60);
   boolean("FAST MOVE LIGHTING",v.previewLighting);boolean("TEMPORAL EXPERIMENT",v.temporal);boolean("STEREO REUSE EXP",v.stereoReuse);
   boolean("ADAPTIVE TILES EXP",v.adaptiveTiles);boolean("FIXED FOVEATION EXP",v.foveated);real("RELAXATION EXP",v.relaxation,.05f,1,1.5f);
   integer("HISTORY FRAMES",v.historyFrames,1,1,30);integer("REFRESH EVERY N",v.refreshRate,1,1,8);
   real("FIELD OF VIEW",v.fov,5,20,100);real("MOVE SPEED",scene.camera.speed,.1f,.01f,10);
   boolean("SURFACE SLOWDOWN",scene.camera.surfaceSpeed);real("SLOWDOWN DISTANCE",scene.camera.surfaceRange,.1f,.001f,20);real("MIN SPEED FRACTION",scene.camera.minimumSpeed,.005f,.0001f,1);
  }else if(tab==2){Formula& f=scene.formula;
   boolean("WORLD REPEAT",f.repeat);real("REPEAT X SPACING",f.repeatPeriod.x,1,0,1000);real("REPEAT Y SPACING",f.repeatPeriod.y,1,0,1000);real("REPEAT Z SPACING",f.repeatPeriod.z,1,0,1000);
   integer("ITERATIONS",f.iterations,1,1,32);real("BAILOUT",f.bailout,1,2,256);boolean("LOG DISTANCE",f.logarithmic);
   integer("TERMINAL SHAPE",f.terminal,1,0,2);real("TERMINAL RADIUS",f.terminalRadius,.1f,.01f,100);
   real("DERIVATIVE SCALE",f.derivativeScale,.25f,1,100);boolean("JULIA MODE",f.julia);
   real("JULIA X",f.constant.x,.05f,-100,100);real("JULIA Y",f.constant.y,.05f,-100,100);real("JULIA Z",f.constant.z,.05f,-100,100);
   integer("EDIT STAGE",stageIndex,1,0,int(f.stages.size())-1);
   action("OPEN STAGE EDITOR","A OPEN",[&](){tab=3;selected=0;});
   action("ADD STAGE","A ADD OFFSET",[&](){if(f.stages.size()<MaxStages){f.stages.push_back(newStage(Kind::Offset));stageIndex=int(f.stages.size())-1;changed=true;}else status="MAXIMUM 12 STAGES";});
  }else if(tab==3){Formula& f=scene.formula;stageIndex=std::min(stageIndex,int(f.stages.size())-1);Stage& a=f.stages[stageIndex];
   integer("STAGE INDEX",stageIndex,1,0,int(f.stages.size())-1);
   fields.push_back({{"OPERATION",kindName(a.kind)},[&](int d){int k=(int(a.kind)+d+int(Kind::Count))%int(Kind::Count);a=newStage(Kind(k));changed=true;},{}});
   boolean("ENABLED",a.enabled);
   const char* labels[3]={"PARAMETER A","PARAMETER B","PARAMETER C"};
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
    real(labels[0],a.a,.05f,a.kind==Kind::Bulb||a.kind==Kind::Menger?1.1f:a.kind==Kind::SphereFold?.001f:-100,a.kind==Kind::Bulb?16:100);
    if(a.kind!=Kind::BoxFold){real(labels[1],a.b,.05f,a.kind==Kind::SphereFold?.001f:a.kind==Kind::Bulb?-4:-100,a.kind==Kind::Bulb?4:100);
     if(a.kind!=Kind::SphereFold&&a.kind!=Kind::Scale)real(labels[2],a.c,.05f,a.kind==Kind::Bulb?-4:-100,a.kind==Kind::Bulb?4:100);
    }
   }
   action("MOVE EARLIER","A REORDER",[&](){if(stageIndex>0){std::swap(f.stages[stageIndex],f.stages[stageIndex-1]);--stageIndex;changed=true;}});
   action("MOVE LATER","A REORDER",[&](){if(stageIndex+1<int(f.stages.size())){std::swap(f.stages[stageIndex],f.stages[stageIndex+1]);++stageIndex;changed=true;}});
   action("DUPLICATE STAGE","A COPY",[&](){if(f.stages.size()<MaxStages){Stage copy=a;f.stages.insert(f.stages.begin()+stageIndex+1,copy);++stageIndex;changed=true;}});
   action("DELETE STAGE","A REMOVE",[&](){if(f.stages.size()>1){f.stages.erase(f.stages.begin()+stageIndex);stageIndex=std::min(stageIndex,int(f.stages.size())-1);changed=true;}else status="KEEP AT LEAST ONE STAGE";});
  }else if(tab==4){Settings& v=scene.settings;
   const char* names[]={"RUST","SEA GLASS","NEON ROSE","BONE"};
   auto cyclePalette=[&](int d){v.palette=float((int(v.palette)+d+4)%4);v.customGradient=false;changed=true;};
   fields.push_back({{"PALETTE",names[int(v.palette)%4]},cyclePalette,[cyclePalette](){cyclePalette(1);}});
   boolean("CUSTOM GRADIENT",v.customGradient);
   auto colorField=[&](std::string label,Vec& c){char hex[8];std::snprintf(hex,sizeof(hex),"%02X%02X%02X",int(clamp(c.x,0,1)*255+.5f),int(clamp(c.y,0,1)*255+.5f),int(clamp(c.z,0,1)*255+.5f));
    action(label,hex,[&,label](){char initial[8];std::snprintf(initial,sizeof(initial),"%02X%02X%02X",int(c.x*255+.5f),int(c.y*255+.5f),int(c.z*255+.5f));std::string text=initial;
     if(keyboard(label+" HEX RRGGBB",text)){if(text.size()==7&&text[0]=='#')text.erase(0,1);char* end=nullptr;unsigned long value=std::strtoul(text.c_str(),&end,16);
      if(text.size()==6&&text.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos&&*end=='\0'){c={float((value>>16)&255)/255,float((value>>8)&255)/255,float(value&255)/255};v.customGradient=true;changed=true;}else status="USE SIX HEX DIGITS RRGGBB";}
    });
   };
   colorField("GRADIENT START",v.gradientLow);colorField("GRADIENT END",v.gradientHigh);
   real("GRADIENT SCALE",v.gradientScale,.1f,.01f,100);real("GRADIENT OFFSET",v.gradientOffset,.05f,-100,100);boolean("REPEAT GRADIENT",v.gradientRepeat);
   real("LIGHT YAW",v.lightYaw,.1f,-6.3f,6.3f);real("LIGHT PITCH",v.lightPitch,.1f,-1.5f,1.5f);real("EXPOSURE",v.exposure,.1f,.1f,4);
   real("SPECULAR",v.specular,.05f,0,1);real("FOG DENSITY",v.fog,.01f,0,1);
   real("CAMERA X",scene.camera.position.x,.1f,-10000,10000);real("CAMERA Y",scene.camera.position.y,.1f,-10000,10000);real("CAMERA Z",scene.camera.position.z,.1f,-10000,10000);
   real("CAMERA YAW",scene.camera.yaw,.1f,-6.3f,6.3f);real("CAMERA PITCH",scene.camera.pitch,.1f,-1.5f,1.5f);
  }else{
   integer("SCENE SLOT",slot,1,0,7);
   auto path=[&](){return std::string("sdmc:/3ds/Re-fract/scene-")+std::to_string(slot)+".rfs";};
   action("SAVE SCENE","A SAVE / REPLACE",[&,path](){saveScene(scene,path(),status);});
   action("LOAD SCENE","A LOAD",[&,path](){if(loadScene(scene,path(),status)){changed=true;stageIndex=0;presetIndex=-1;}});
   action("EXPORT STEREO PPM","A SAVE / REPLACE",[&](){
    std::string base="sdmc:/3ds/Re-fract/image-"+std::to_string(slot);
    if(savePPM(renderer.image(0),base+"-left.ppm",status)&&savePPM(renderer.image(1),base+"-right.ppm",status))status=renderer.complete()?"STEREO PAIR EXPORTED":"PARTIAL FRAME EXPORTED";
   });
   action("QUALITY RENDER","A START",[&](){scene.settings.quality=true;changed=true;status="QUALITY RENDER STARTED";});
   action("RENDER AGAIN","A RESTART",[&](){changed=true;});
   action("UI FRAME / TRACE MS",number(renderer.measuredFrameMs())+" / "+number(workMs),[](){});
   action("RAYS / DE QUERIES",std::to_string(renderer.profile().rays)+" / "+std::to_string(renderer.profile().distanceQueries),[](){});
   action("REUSED / BATCHES",std::to_string(renderer.profile().reused)+" / "+std::to_string(renderer.profile().batches),[](){});
   action("RESET COUNTERS","A RESET",[&](){renderer.resetProfile();});
   action("CONTROLS","A HELP",[&](){status="PAD MOVE C-STICK LOOK ZL/ZR UP/DOWN";});
  }
  }
  selected=std::max(0,std::min(selected,int(fields.size())-1));
  u32 direction=held&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT);
  u32 repeat=down&direction;
  if(direction!=lastDirection){nextRepeat=now+300;lastDirection=direction;}
  else if(direction&&now>=nextRepeat){repeat=direction;nextRepeat=now+90;}
  if(repeat&KEY_DUP)selected=(selected+int(fields.size())-1)%int(fields.size());
  if(repeat&KEY_DDOWN)selected=(selected+1)%int(fields.size());
  std::unique_ptr<Scene> before;int beforeStage=stageIndex;
  bool activate=down&KEY_A;
  if(down&KEY_TOUCH){touchPosition touch;hidTouchRead(&touch);
   if(touch.py>=36&&touch.py<53){tab=std::min(5,std::max(0,(int(touch.px)-6)/52));selected=0;}
   else if(touch.py>=70&&touch.py<190){int row=int(touch.py-70)/15+(selected/8)*8;
    if(row<int(fields.size())){selected=row;if(touch.px<45)repeat|=KEY_DLEFT;else if(touch.px>280)repeat|=KEY_DRIGHT;else activate=true;}
   }
  }
  if((repeat&(KEY_DLEFT|KEY_DRIGHT))||activate){before.reset(new Scene(scene));menuDirty=true;}
  if((repeat&KEY_DLEFT)&&fields[selected].adjust)fields[selected].adjust(-1);
  if((repeat&KEY_DRIGHT)&&fields[selected].adjust)fields[selected].adjust(1);
  if(activate&&fields[selected].activate)fields[selected].activate();
  if(changed){std::string error;if(!scene.formula.validate(error)){if(before)scene=*before;stageIndex=beforeStage;status=error;}}
  float previousSlider=slider;
  bool sliderChanged=sliderControl.update(scene.settings,osGet3DSliderState());slider=sliderControl.strength;
  if(sliderChanged)sliderUntil=now+250;
  // Enabling stereo at the physical 2D stop does not alter any camera ray.
  if(changed&&before&&fields[selected].row.label=="STEREO"&&previousSlider==0&&slider==0)changed=false;
  bool activeStereo=slider>0;
  if(activeStereo!=displayStereo){topCopies[0].reset();topCopies[1].reset();displayStereo=activeStereo;}
  gfxSet3D(activeStereo);
  renderer.beginFrame(scene,motion||now<sliderUntil,changed,slider);
  Rays rays(scene);uint64_t start=osGetTime(),beforeRays=renderer.rays();
  while(!renderer.complete()){
   renderer.step(scene,rays,slider);
   if(float(osGetTime()-start)>=scene.settings.budgetMs)break;
  }
  workMs=float(osGetTime()-start);
  // Fields are rebuilt next frame; do not dereference captures after a vector edit.
  std::vector<Row> rows;for(const Field& f:fields)rows.push_back(f.row);
  drawPanel(bottom,tab,selected,rows,status,presetIndex<0?"CUSTOM SCENE":presetName(presetIndex),renderer.currentBlock(),renderer.progress(scene),scene.settings.quality,workMs);
  if(tab==4){Settings gradient=scene.settings;gradient.gradientScale=1;gradient.gradientOffset=0;gradient.gradientRepeat=false;
   for(int x=0;x<300;++x){Vec c=gradientColor(gradient,float(x)/299);bottom.rect(10+x,67,1,3,{uint8_t(c.x*255),uint8_t(c.y*255),uint8_t(c.z*255)});}}
  topCopies[0].copy(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr),renderer.image(0),renderer.imageRevision());
  if(activeStereo)topCopies[1].copy(gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,nullptr,nullptr),renderer.image(1),renderer.imageRevision());
  blit(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr),bottom.pixels,320);
  gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
  renderer.endFrame(scene,float(osGetTime()-now),workMs,renderer.rays()-beforeRays);
 }
 worker.shutdown();gfxExit();return 0;
}
