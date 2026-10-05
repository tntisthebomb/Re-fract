#include <3ds.h>
#include "engine.hpp"
#include "ui.hpp"
#include "stereo_worker.hpp"
#include "framebuffer.hpp"
#include "gpu_surface.hpp"
#include "trace_benchmark.hpp"
#include "distance_field.hpp"
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
 convertFramebuffer(frame,pixels.data(),width);
}
bool pickColor(Vec& color){
 Vec hsv=colorHSV(color);Canvas panel(320,240);bool dirty=true;
 while(aptMainLoop()){
  hidScanInput();u32 down=hidKeysDown(),held=hidKeysHeld();bool apply=down&KEY_A,cancel=down&(KEY_B|KEY_START);
  if(held&KEY_TOUCH){touchPosition touch;hidTouchRead(&touch);float dx=int(touch.px)-92,dy=int(touch.py)-116,r=std::sqrt(dx*dx+dy*dy);
   if(r<=84){hsv.x=std::atan2(dy,dx)/6.2831853f;hsv.x-=std::floor(hsv.x);hsv.y=clamp(r/80,0,1);dirty=true;}
   else if(touch.px>=188&&touch.px<=234&&touch.py>=40&&touch.py<=185){hsv.z=clamp(1-float(int(touch.py)-45)/135,0,1);dirty=true;}
   if(down&KEY_TOUCH&&touch.py>=222){apply=touch.px>=176;cancel=touch.px<=142;}
  }
  if(held&(KEY_DLEFT|KEY_DRIGHT|KEY_DUP|KEY_DDOWN|KEY_L|KEY_R)){
   hsv.x+=(held&KEY_DRIGHT?.005f:0)-(held&KEY_DLEFT?.005f:0);hsv.x-=std::floor(hsv.x);
   hsv.y=clamp(hsv.y+(held&KEY_DUP?.01f:0)-(held&KEY_DDOWN?.01f:0),0,1);hsv.z=clamp(hsv.z+(held&KEY_R?.01f:0)-(held&KEY_L?.01f:0),0,1);dirty=true;
  }
  if(cancel)return false;
  if(apply){color=hsvColor(hsv.x,hsv.y,hsv.z);return true;}
  if(dirty){drawColorPicker(panel,hsv.x,hsv.y,hsv.z);dirty=false;}
  u8* framebuffer=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr);blit(framebuffer,panel.pixels,320);GSPGPU_FlushDataCache(framebuffer,320*240*3);gfxScreenSwapBuffers(GFX_BOTTOM,false);gspWaitForVBlank();
 }
 return false;
}
struct FrameCopyCache {
 u8* addresses[2]={nullptr,nullptr};uint64_t revisions[2]={0,0};bool valid[2]={false,false};
 void copy(u8* address,const std::vector<Color>& pixels,uint64_t revision,int width=W){
  int slot=address==addresses[0]?0:address==addresses[1]?1:addresses[0]==nullptr?0:1;
  if(!valid[slot]||addresses[slot]!=address||revisions[slot]!=revision){
   blit(address,pixels,width);
   // libctru's gfxFlushBuffers flushes every screen unconditionally. Flush
   // exactly this modified buffer before marking its revision visible to GSP.
   Result flushed=GSPGPU_FlushDataCache(address,width*H*3);
   addresses[slot]=address;revisions[slot]=revision;valid[slot]=R_SUCCEEDED(flushed);
  }
 }
 void reset(){valid[0]=valid[1]=false;}
};
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
}
int main(){
 gfxInitDefault();gfxSet3D(false);osSetSpeedupEnable(true);
 FrameCopyCache topCopies[2],bottomCopies;bool displayStereo=false;uint64_t panelRevision=1;float panelWorkMs=-1;
 bool new3ds=false;APT_CheckNew3DS(&new3ds);
 mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/Re-fract",0777);
 auto sceneOwner=std::make_unique<Scene>();auto rendererOwner=std::make_unique<Renderer>();
 Scene& scene=*sceneOwner;Renderer& renderer=*rendererOwner;Canvas bottom(320,240);
 StereoWorker worker(new3ds);
 std::shared_ptr<DistanceField> distanceField;std::unique_ptr<Scene> detailScene;std::unique_ptr<TraceBenchmark> benchmark;bool benchmarkActive=false;
 GpuSurface gpuSurface;bool meshNavigation=false,captureRequested=false;bool refreshPending=false;
 if(worker.available()){renderer.setBatchShader(StereoWorker::shade,&worker);renderer.setParallelFor(StereoWorker::parallelFor,&worker);}
 int tab=0,selected=0,presetIndex=0,stageIndex=0,slot=0,lightIndex=0;float slider=0,workMs=0;StereoSlider sliderControl;uint64_t sliderUntil=0;
 std::string status=worker.available()?"NEW 3DS / TWO CPU BATCHES":new3ds?"NEW 3DS / SINGLE CPU FALLBACK":"OLD 3DS / USE 16X PREVIEW";
 if(!new3ds){scene.settings.previewBlock=16;scene.settings.budgetMs=5;}
 renderer.invalidate(scene,false);
 uint64_t previous=osGetTime(),nextRepeat=0;u32 lastDirection=0;
 bool screenshotRequested=false,exitRequested=false;
 bool changed=false,menuDirty=true;int menuTab=-1;uint64_t nextMenuRefresh=0;std::vector<Field> fields;
 while(aptMainLoop()){
  uint64_t now=osGetTime();float dt=clamp(float(now-previous)*.001f,.001f,.05f);previous=now;
  hidScanInput();u32 down=hidKeysDown(),held=hidKeysHeld();
  if(down&KEY_START)screenshotRequested=true;
  changed=false;bool panelRefresh=false;
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
  fields.clear();menuDirty=false;menuTab=tab;nextMenuRefresh=now+250;panelRefresh=true;
  auto action=[&](std::string label,std::string value,std::function<void()> fn){fields.push_back({{label,value},{},fn});};
  auto real=[&](std::string label,float& v,float step,float lo,float hi){
   if(label=="GRADIENT SCALE"){lo=-10000;hi=10000;}else if(label=="GRADIENT OFFSET"){lo=-10000;hi=10000;}
   else if(label=="HIT EPSILON"||label=="MIN HIT EPSILON"){lo=1e-9f;hi=1;}
   else if(label=="FAR CLIP"){lo=.01f;hi=10000;}else if(label=="STEP SAFETY"){lo=.001f;hi=4;}
   else if(label=="EXPOSURE"||label=="SUN STRENGTH"||label=="COLOR EMISSION"||label=="BLOOM HALO"){lo=0;hi=100;}
   else if(label=="SPECULAR"||label=="INDIRECT STRENGTH"){lo=0;hi=20;}
   else if(label=="POINT INTENSITY"){lo=0;hi=10000;}else if(label=="POINT RANGE"||label=="INDIRECT RANGE"||label=="FOCUS DISTANCE"||label=="CONVERGENCE"){lo=.0001f;hi=10000;}
   else if(label=="LENS APERTURE"){lo=0;hi=100;}else if(label=="FOG DENSITY"){lo=0;hi=100;}
   else if(label=="MOVE SPEED"){lo=.00001f;hi=1000;}else if(label=="FRAME BUDGET MS"){lo=1;hi=100;}
   else if(label=="BAILOUT"){lo=2;hi=1000000;}else if(label=="TERMINAL RADIUS"){lo=.000001f;hi=10000;}
   else if(label=="DERIVATIVE SCALE"){lo=.001f;hi=10000;}
   else if(label=="FIELD OF VIEW"){lo=1;hi=175;}
   else if(label=="POWER"){lo=1.01f;hi=32;}else if(label=="THETA MULTIPLIER"||label=="PHI MULTIPLIER"){lo=-32;hi=32;}
   else if(label=="FOLD X"||label=="FOLD Y"||label=="FOLD Z"||label=="MINIMUM RADIUS"||label=="FIXED RADIUS"){lo=.000002f;hi=10000;}
   else if(label=="INVERSION RADIUS"){lo=.000001f;hi=1000;}
   else if(label=="SCALE"||label=="C MULTIPLIER"||label=="X OFFSET"||label=="Y OFFSET"||label=="Z OFFSET"||label=="FOLD LIMIT"){lo=-10000;hi=10000;}


   fields.push_back({{label,number(v)},[&,step,lo,hi](int d){v=clamp(v+d*step,lo,hi);changed=true;},
    [&,label,lo,hi](){std::string input=number(v);if(keyboard(label,input)){char* end=nullptr;errno=0;float n=std::strtof(input.c_str(),&end);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&std::isfinite(n)&&n>=lo&&n<=hi){v=n;changed=true;}else status="INVALID NUMBER / RANGE";
    }} });
  };
  auto integer=[&](std::string label,int& v,int step,int lo,int hi){
   if(label=="RAY STEPS"||label=="SHADOW STEPS"||label=="INDIRECT STEPS")hi=4096;
   else if(label=="ITERATIONS"||label=="MOVE ITERATIONS"||label=="DETAIL ITERATION CAP")hi=256;
   else if(label=="AO SAMPLES"||label=="INDIRECT SAMPLES")hi=64;
   else if(label=="LIGHTING PASSES")hi=4096;

   fields.push_back({{label,std::to_string(v)},[&,step,lo,hi](int d){v=std::max(lo,std::min(hi,v+d*step));changed=true;},
    [&,label,lo,hi](){std::string input=std::to_string(v);if(keyboard(label,input)){char* end=nullptr;errno=0;long n=std::strtol(input.c_str(),&end,10);
     if(end!=input.c_str()&&*end=='\0'&&errno==0&&n>=lo&&n<=hi){v=int(n);changed=true;}else status="INVALID INTEGER / RANGE";
    }} });
  };
  auto boolean=[&](std::string label,bool& v){auto fn=[&](){v=!v;changed=true;};fields.push_back({{label,v?"ON":"OFF"},[fn](int){fn();},fn});};
  auto choice=[&](std::string label,int& v,const std::vector<int>& values){fields.push_back({{label,std::to_string(v)},
   [&,values](int dir){auto it=std::find(values.begin(),values.end(),v);int i=it==values.end()?0:int(it-values.begin());i=(i+dir+int(values.size()))%int(values.size());v=values[i];changed=true;},{}});};
  if(tab==0){for(int i=0;i<PresetCount;++i)action(presetName(i),presetIndex==i?"CURRENT":"A LOAD",[&,i](){scene.formula=preset(i);presetIndex=i;stageIndex=0;scene.camera=presetCamera(i);if(i>=24)scene.settings=presetSettings(i);scene.settings.farClip=40;scene.settings.convergence=-scene.camera.position.z;changed=true;status="PRESET LOADED";});}
  else if(tab==1){Settings& v=scene.settings;
   boolean("QUALITY MODE",v.quality);choice("PREVIEW BLOCK",v.previewBlock,{2,4,8,16});choice("STILL BLOCK",v.stillBlock,{1,2,4,8,16});
   const char* scaling[]={"NEAREST","BILINEAR","BICUBIC"};fields.push_back({{"UPSCALING",scaling[v.upscale]},[&](int d){v.upscale=(v.upscale+d+3)%3;changed=true;},{}});integer("INTERLACE LANES",v.interlace,1,1,8);
   integer("RAY STEPS",v.steps,8,8,256);real("HIT EPSILON",v.epsilon,.0005f,.000001f,.05f);boolean("ZOOM PRECISION EXP",v.adaptivePrecision);real("MIN HIT EPSILON",v.minEpsilon,.000001f,.000001f,.001f);real("PIXEL TOLERANCE",v.pixelTolerance,.05f,.05f,2);real("STEP SAFETY",v.safety,.05f,.05f,1);
   real("FAR CLIP",v.farClip,1,1,100);real("FRAME BUDGET MS",v.budgetMs,1,1,20);boolean("AUTO REFINE",v.autoRefine);boolean("ADAPT DETAIL EXP",v.adaptiveDetail);integer("MOVE ITERATIONS",v.previewIterations,1,1,32);integer("DETAIL ITERATION CAP",v.detailIterations,1,1,32);
   boolean("DEPTH OF FIELD",v.dof);real("LENS APERTURE",v.aperture,.01f,0,1);real("FOCUS DISTANCE",v.focusDistance,.1f,.001f,100);choice("LENS SAMPLES",v.dofSamples,{1,2,4,8,16});
   action("FOCUS AT CONVERGENCE","A MATCH DISTANCE",[&](){v.focusDistance=v.convergence;changed=true;});
   choice("QUALITY SAMPLES",v.samples,{1,2,4});integer("AO SAMPLES",v.ao,1,0,6);integer("SHADOW STEPS",v.shadow,4,0,64);
   boolean("STEREO",v.stereo);real("EYE SEPARATION",v.eyeSeparation,.005f,0,.2f);real("CONVERGENCE",v.convergence,.2f,.2f,30);
   boolean("PARALLEL BATCHES",v.parallel);
   boolean("GPU SURFACE CACHE",v.gpuCache);boolean("GPU AUTO RECAPTURE",v.gpuAutoRefresh);choice("GPU MESH SPACING",v.meshStride,{4,8,16});real("GPU EDGE REJECTION",v.meshEdge,.01f,.005f,.5f);
   real("GPU NEAR CLIP",v.meshNear,.001f,.0001f,.5f);
   action("CAPTURE GPU SURFACE","A AFTER RENDER",[&](){captureRequested=true;});
   action("RESUME CPU RENDER","A TRACE HERE",[&](){gpuSurface.synchronize();meshNavigation=false;changed=true;status="CPU RENDER AT CURRENT CAMERA";topCopies[0].reset();topCopies[1].reset();});
   integer("BATCH SIZE",v.batchSize,1,1,32);boolean("ADAPT RESOLUTION",v.adaptiveResolution);integer("TARGET REFRESH FPS",v.targetFps,5,15,60);
   boolean("FAST MOVE LIGHTING",v.previewLighting);boolean("DISTANCE FIELD EXP",v.distanceField);real("FIELD GRID SPACING",v.fieldSpacing,.05f,.05f,1);boolean("TEMPORAL EXPERIMENT",v.temporal);boolean("STEREO REUSE EXP",v.stereoReuse);
   boolean("DEPTH PREPASS",v.depthPrepass);choice("PREPASS BLOCK",v.prepassBlock,{4,8,16,32});real("PREPASS START FRACTION",v.prepassSafety,.05f,0,.9f);boolean("ADAPT RAY BUDGET",v.adaptiveRayBudget);integer("SKY RAY MIN STEPS",v.rayMinSteps,4,4,4096);boolean("RETRY SHORT RAYS",v.rayBudgetRetry);
   boolean("ADAPT EMPTY SPACE",v.adaptiveEmpty);boolean("ADAPT SKY EDGES",v.adaptiveSkyEdges);integer("SKY PROBE EVERY N",v.emptyProbe,1,1,64);integer("EDGE PROBE EVERY N",v.skyEdgeProbe,1,1,64);integer("SKY NEIGHBORS NEEDED",v.skyNeighbors,1,1,9);integer("DENSE REFRESH PASSES",v.emptyRefresh,1,0,256);
   boolean("ADAPTIVE TILES EXP",v.adaptiveTiles);boolean("FIXED FOVEATION EXP",v.foveated);real("RELAXATION EXP",v.relaxation,.05f,1,1.5f);
   integer("HISTORY FRAMES",v.historyFrames,1,1,30);integer("REFRESH EVERY N",v.refreshRate,1,1,8);
   real("FIELD OF VIEW",v.fov,5,20,100);real("MOVE SPEED",scene.camera.speed,.1f,.01f,10);
   boolean("SURFACE SLOWDOWN",scene.camera.surfaceSpeed);real("SLOWDOWN DISTANCE",scene.camera.surfaceRange,.1f,.001f,20);real("MIN SPEED FRACTION",scene.camera.minimumSpeed,.005f,.0001f,1);
  }else if(tab==2){Formula& f=scene.formula;
   boolean("ALGEBRAIC BULB EXP",f.algebraicBulb);
   boolean("WORLD REPEAT",f.repeat);real("REPEAT X SPACING",f.repeatPeriod.x,1,0,1000);real("REPEAT Y SPACING",f.repeatPeriod.y,1,0,1000);real("REPEAT Z SPACING",f.repeatPeriod.z,1,0,1000);
   integer("ITERATIONS",f.iterations,1,1,32);real("BAILOUT",f.bailout,1,2,256);boolean("LOG DISTANCE",f.logarithmic);
   integer("TERMINAL SHAPE",f.terminal,1,0,3);real("TERMINAL RADIUS",f.terminalRadius,.1f,.01f,100);
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
    real(labels[0],a.a,.05f,a.kind==Kind::Bulb||a.kind==Kind::Menger?1.1f:a.kind==Kind::SphereFold?.001f:-100,a.kind==Kind::Bulb?16:100);
    if(a.kind!=Kind::BoxFold&&a.kind!=Kind::Inversion){real(labels[1],a.b,.05f,a.kind==Kind::SphereFold?.001f:a.kind==Kind::Bulb?-4:-100,a.kind==Kind::Bulb?4:100);
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
   real("COLOR EMISSION",v.emission,.1f,0,4);real("BLOOM HALO",v.bloom,.1f,0,4);
   auto colorField=[&](std::string label,Vec& c){char hex[8];std::snprintf(hex,sizeof(hex),"%02X%02X%02X",int(clamp(c.x,0,1)*255+.5f),int(clamp(c.y,0,1)*255+.5f),int(clamp(c.z,0,1)*255+.5f));
    action(label,hex,[&,label](){if(pickColor(c)){if(label.rfind("GRADIENT ",0)==0)v.customGradient=true;changed=true;}bottomCopies.reset();topCopies[0].reset();topCopies[1].reset();menuDirty=true;});
   };
   boolean("BOUNDED COLOR MAP",v.boundedGradient);integer("GRADIENT STOPS",v.gradientStops,1,2,5);
   colorField("GRADIENT START",v.gradientLow);
   for(int i=0;i<v.gradientStops-2;++i)colorField("GRADIENT STOP "+std::to_string(i+2),v.gradientMiddle[i]);
   colorField("GRADIENT END",v.gradientHigh);
   action("AUTO FIT GRADIENT","A FIT CURRENT VIEW",[&](){if(renderer.fitGradient(scene)){changed=true;status="GRADIENT FIT TO VISIBLE SURFACE";}else status="FINISH PINHOLE RENDER / NO VARIATION";});
   real("GRADIENT SCALE",v.gradientScale,.1f,.01f,100);real("GRADIENT OFFSET",v.gradientOffset,.05f,-100,100);boolean("REPEAT GRADIENT",v.gradientRepeat);
   real("LIGHT YAW",v.lightYaw,.1f,-6.3f,6.3f);real("LIGHT PITCH",v.lightPitch,.1f,-1.5f,1.5f);real("EXPOSURE",v.exposure,.1f,.1f,4);
   real("SUN STRENGTH",v.sunStrength,.1f,0,4);
   fields.push_back({{"EDIT POINT LIGHT",std::to_string(lightIndex+1)},[&](int d){lightIndex=(lightIndex+d+2)%2;menuDirty=true;},{}});
   PointLight& lamp=v.pointLights[lightIndex];boolean("POINT LIGHT ENABLED",lamp.enabled);boolean("CAMERA RELATIVE LIGHT",lamp.cameraRelative);
   real("POINT X",lamp.position.x,.1f,-10000,10000);real("POINT Y",lamp.position.y,.1f,-10000,10000);real("POINT Z",lamp.position.z,.1f,-10000,10000);
   colorField("POINT COLOR",lamp.color);real("POINT INTENSITY",lamp.intensity,.5f,0,50);real("POINT RANGE",lamp.range,.5f,.1f,100);boolean("POINT SHADOWS",v.pointShadows);
   auto progressive=[&](){v.progressiveLighting=!v.progressiveLighting;if(v.progressiveLighting&&v.giSamples==0)v.giSamples=1;changed=true;};
   fields.push_back({{"PROGRESSIVE LIGHTING",v.progressiveLighting?"ON":"OFF"},[progressive](int){progressive();},progressive});
   boolean("SEPARATE GI RES",v.separateLighting);boolean("ADAPT GI SAMPLES",v.adaptiveLighting);integer("GI MIN PASSES",v.lightingMinPasses,1,2,4096);real("GI ERROR THRESHOLD",v.lightingThreshold,.005f,.0001f,1);integer("GI REFRESH PASSES",v.lightingRefresh,1,0,256);real("GI DEPTH EDGE",v.lightingDepthTolerance,.01f,.001f,1);
   integer("LIGHTING PASSES",v.lightingPasses,1,1,128);choice("LIGHTING BLOCK",v.lightingBlock,{1,2,4,8,16});
   integer("INDIRECT SAMPLES",v.giSamples,1,0,4);integer("INDIRECT STEPS",v.giSteps,4,4,64);
   real("INDIRECT STRENGTH",v.giStrength,.1f,0,2);real("INDIRECT RANGE",v.giRange,.1f,.1f,10);colorField("SKY COLOR",v.skyColor);
   real("SPECULAR",v.specular,.05f,0,1);real("FOG DENSITY",v.fog,.01f,0,1);
   real("CAMERA X",scene.camera.position.x,.1f,-10000,10000);real("CAMERA Y",scene.camera.position.y,.1f,-10000,10000);real("CAMERA Z",scene.camera.position.z,.1f,-10000,10000);
   real("CAMERA YAW",scene.camera.yaw,.1f,-6.3f,6.3f);real("CAMERA PITCH",scene.camera.pitch,.1f,-1.5f,1.5f);
  }else{
   action("SAVE SCREENSHOT","START / A JPEG-MPO",[&](){screenshotRequested=true;});
   action("EXIT APP","A RETURN HOME",[&](){exitRequested=true;});
   integer("SCENE SLOT",slot,1,0,7);
   auto path=[&](){return std::string("sdmc:/3ds/Re-fract/scene-")+std::to_string(slot)+".rfs";};
   action("SAVE SCENE","A SAVE / REPLACE",[&,path](){saveScene(scene,path(),status);});
   action("LOAD SCENE","A LOAD",[&,path](){if(loadScene(scene,path(),status)){changed=true;stageIndex=0;presetIndex=-1;}});
   action("EXPORT STEREO PPM","A SAVE / REPLACE",[&](){
    if(meshNavigation){status="RESUME CPU BEFORE EXPORTING CURRENT VIEW";return;}
    std::string base="sdmc:/3ds/Re-fract/image-"+std::to_string(slot);
    if(savePPM(renderer.image(0),base+"-left.ppm",status)&&savePPM(renderer.image(1),base+"-right.ppm",status))status=renderer.complete()?"STEREO PAIR EXPORTED":"PARTIAL FRAME EXPORTED";
   });
   action("QUALITY RENDER","A START",[&](){scene.settings.quality=true;changed=true;status="QUALITY RENDER STARTED";});
   action("RENDER AGAIN","A RESTART",[&](){changed=true;});
   action("FIELD CACHE READY",distanceField?std::to_string(int(distanceField->progress()*100))+"%":"OFF",[](){});
   action("BENCH CURRENT VIEW","A 1500 MONO SAMPLES",[&](){benchmark=std::make_unique<TraceBenchmark>(scene);benchmarkActive=true;status="BENCHMARK RUNNING / FROZEN MONO VIEW";});
   action("BENCH MOVE PREVIEW","A 1500 MONO SAMPLES",[&](){benchmark=std::make_unique<TraceBenchmark>(scene,distanceField&&distanceField->ready()?distanceField:nullptr,true);benchmarkActive=true;status="BENCHMARK / MOVE PREVIEW SNAPSHOT";});
   action("BENCH SAMPLES",benchmark?std::to_string(benchmark->completedSamples())+" / 1500":"NOT RUN",[](){});
   action("BENCH RAYS / SECOND",benchmark?number(benchmark->raysPerSecond()):"NOT RUN",[](){});
   action("CANCEL BENCHMARK","A CANCEL",[&](){benchmarkActive=false;});
   action("LAST FRAME MS",renderer.lastCompletedFrameBlock()?number(renderer.lastCompletedFrameMs()):"NOT COMPLETED",[](){});
   action("LAST FRAME SCALE / EYES",std::to_string(renderer.lastCompletedFrameBlock())+"X / "+std::to_string(renderer.lastCompletedFrameEyes()),[](){});
   action("LAST REALTIME FRAME MS",renderer.lastRealtimeFrameBlock()?number(renderer.lastRealtimeFrameMs()):"NOT COMPLETED",[](){});
   action("LAST REALTIME SCALE",std::to_string(renderer.lastRealtimeFrameBlock())+"X",[](){});
   action("UI FRAME / TRACE MS",number(renderer.measuredFrameMs())+" / "+number(workMs),[](){});
   action("LIGHTING PASSES DONE",std::to_string(renderer.accumulatedPasses())+" / "+std::to_string(scene.settings.lightingPasses),[](){});
   action("GPU MODE / TRIANGLES",std::string(meshNavigation?"ON / ":"OFF / ")+std::to_string(gpuSurface.triangles()),[](){});
   action("GPU DRAW MS",number(gpuSurface.drawingMs()),[](){});
   action("RAYS / DE QUERIES",std::to_string(renderer.profile().rays)+" / "+std::to_string(renderer.profile().distanceQueries),[](){});
   action("GI CELLS SKIPPED",std::to_string(renderer.profile().lightingSkipped),[](){});
   action("DEPTH STARTS / RAY RETRIES",std::to_string(renderer.profile().depthStarts)+" / "+std::to_string(renderer.profile().budgetRetries),[](){});
   action("SKY CELLS SKIPPED",std::to_string(renderer.profile().skySkipped),[](){});
   action("REUSE EVENTS / BATCHES",std::to_string(renderer.profile().reused)+" / "+std::to_string(renderer.profile().batches),[](){});
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
  bool materialEditOnly=!changed;
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
  if(activeStereo!=displayStereo){gpuSurface.synchronize();topCopies[0].reset();topCopies[1].reset();displayStereo=activeStereo;}
  gfxSet3D(activeStereo);
  if(changed&&materialEditOnly&&before&&tab==4&&!motion&&!meshNavigation&&now>=sliderUntil&&!sliderChanged){
   const std::string& label=fields[selected].row.label;
   bool material=label=="PALETTE"||label=="CUSTOM GRADIENT"||label.rfind("GRADIENT ",0)==0||label=="SKY COLOR"||label=="AUTO FIT GRADIENT"||label=="BOUNDED COLOR MAP"||label=="COLOR EMISSION"||label=="BLOOM HALO"||label=="GRADIENT SCALE"||label=="GRADIENT OFFSET"||label=="REPEAT GRADIENT"||label=="EXPOSURE"||label=="FOG DENSITY";
   if(material&&renderer.recolor(scene)){changed=false;status="COLOR UPDATED / GEOMETRY REUSED";}
  }
  if(changed&&meshNavigation){gpuSurface.synchronize();meshNavigation=false;topCopies[0].reset();topCopies[1].reset();}
  if(captureRequested){captureRequested=false;SurfaceMesh mesh;
   if(!meshNavigation&&!motion&&renderer.captureSurface(scene,mesh)&&gpuSurface.upload(mesh)){
    meshNavigation=true;scene.settings.quality=false;refreshPending=false;status="GPU SURFACE / "+std::to_string(gpuSurface.triangles())+" TRIANGLES";
   }else status="ENABLE CACHE / NO BLOOM-DOF-GI / FINISH";
   menuDirty=true;
  }
  Scene* rendering=&scene;
  if(scene.settings.adaptiveDetail){
   if(!detailScene||changed)detailScene=std::make_unique<Scene>(scene);
   detailScene->settings=scene.settings;detailScene->camera=scene.camera;
   detailScene->formula.iterations=renderingIterations(scene,motion||now<sliderUntil);rendering=detailScene.get();
  }
  bool refresh=meshNavigation&&scene.settings.gpuAutoRefresh;
  if(refresh&&(motion||sliderChanged))refreshPending=true;
  if(!meshNavigation||refresh)renderer.beginFrame(*rendering,motion||now<sliderUntil,changed,slider);
  if(scene.settings.distanceField&&!scene.settings.adaptiveDetail&&(!distanceField||changed||!distanceField->containsCamera(scene.camera.position)))distanceField=std::make_shared<DistanceField>(scene);
  if(!scene.settings.distanceField||scene.settings.adaptiveDetail)distanceField.reset();
  Rays rays(*rendering);rays.field=scene.settings.adaptiveDetail?nullptr:distanceField.get();uint64_t start=osGetTime(),beforeJobs=renderer.jobs(),beforeRevision=renderer.imageRevision();
  if(distanceField&&!distanceField->ready()&&!benchmarkActive&&!meshNavigation){
   distanceField->build(worker.available()&&scene.settings.parallel?StereoWorker::parallelFor:nullptr,&worker);
  }
  while(!benchmarkActive&&(!meshNavigation||refresh)&&!renderer.complete()){
   renderer.step(*rendering,rays,slider);
   if(float(osGetTime()-start)>=scene.settings.budgetMs)break;
  }
  if(benchmarkActive){
   // Hardware tick timing excludes UI, framebuffer transfers and VBlank.
   uint64_t ticks=svcGetSystemTick();
   do{benchmark->step(worker.available()?StereoWorker::shade:nullptr,&worker);}
   while(!benchmark->complete()&&float(osGetTime()-start)<scene.settings.budgetMs);
   benchmark->recordMs(float((svcGetSystemTick()-ticks)*1000.0/SYSCLOCK_ARM11));
   if(benchmark->complete()){
    benchmarkActive=false;bool saved=benchmark->save("sdmc:/3ds/Re-fract/benchmark-"+std::to_string(slot)+".csv");
    status=saved?"BENCHMARK DONE / CSV SAVED":"BENCHMARK DONE / CSV WRITE FAILED";menuDirty=true;
   }
  }
  workMs=float(osGetTime()-start);
  if(refresh&&refreshPending&&!motion&&renderer.complete()){
   SurfaceMesh mesh;if(renderer.captureSurface(scene,mesh)&&gpuSurface.upload(mesh)){
    refreshPending=false;status="GPU CACHE REFRESHED";menuDirty=true;
   }else{refreshPending=false;status="RECAPTURE NEEDS SINGLE SAMPLE / FINE BLOCK";menuDirty=true;}
  }
  // Fields are rebuilt next frame; do not dereference captures after a vector edit.
  // A finished, idle scene does not need a UI redraw or bottom-screen transfer.
  if(panelRefresh||menuDirty||changed||down||repeat||motion||now<sliderUntil||workMs!=panelWorkMs||renderer.imageRevision()!=beforeRevision){
  panelWorkMs=workMs;
  ++panelRevision;std::vector<Row> rows;rows.reserve(fields.size());for(const Field& f:fields)rows.push_back(f.row);
  drawPanel(bottom,tab,selected,rows,status,presetIndex<0?"CUSTOM SCENE":presetName(presetIndex),renderer.currentBlock(),renderer.progress(scene),scene.settings.quality,workMs);
  if(tab==4){Settings gradient=scene.settings;gradient.gradientScale=1;gradient.gradientOffset=0;gradient.gradientRepeat=false;gradient.boundedGradient=false;
   for(int x=0;x<300;++x){Vec c=gradientColor(gradient,float(x)/299);bottom.rect(10+x,67,1,3,{uint8_t(c.x*255),uint8_t(c.y*255),uint8_t(c.z*255)});}}
  }
  if(screenshotRequested){
   screenshotRequested=false;mkdir("sdmc:/3ds/Re-fract/screenshots",0777);
   std::vector<Color> top=renderer.image(0),right;
   bool captured=!meshNavigation||gpuSurface.screenshot(0,top);
   if(captured&&activeStereo){right=renderer.image(1);captured=!meshNavigation||gpuSurface.screenshot(1,right);}
   if(captured){std::vector<Color> both(W*H*2,Color{});std::copy(top.begin(),top.end(),both.begin());
    for(int y=0;y<H;++y)std::copy(bottom.pixels.begin()+y*320,bottom.pixels.begin()+(y+1)*320,both.begin()+(y+H)*W+40);
    std::string base="sdmc:/3ds/Re-fract/screenshots/shot-"+std::to_string(osGetTime());
    bool saved=saveJPEG(top,W,H,base+".jpg",status);
    if(saved&&activeStereo)saved=saveMPO(top,right,base+".mpo",status);
    if(saved)saved=saveJPEG(both,W,H*2,base+"-screens.jpg",status);
    if(saved)status=activeStereo?"JPEG + 3D MPO SAVED":"JPEG SCREENSHOT SAVED";
   }else status="GPU SCREENSHOT FAILED";
   menuDirty=true;renderer.invalidate(scene,motion,true);
  }
  bottomCopies.copy(gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,nullptr,nullptr),bottom.pixels,panelRevision,320);
  if(meshNavigation&&gpuSurface.draw(scene,slider)){gfxScreenSwapBuffers(GFX_BOTTOM,false);}
  else{
   if(meshNavigation){gpuSurface.synchronize();meshNavigation=false;changed=true;renderer.invalidate(scene,false);status="GPU FAILED / CPU FALLBACK";menuDirty=true;topCopies[0].reset();topCopies[1].reset();}
   topCopies[0].copy(gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr),renderer.image(0),renderer.imageRevision());
   if(activeStereo)topCopies[1].copy(gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,nullptr,nullptr),renderer.image(1),renderer.imageRevision());
   gfxSwapBuffers();
  }
  gspWaitForVBlank();
  if(exitRequested)break;
  renderer.endFrame(scene,float(osGetTime()-now),workMs,renderer.jobs()-beforeJobs);
 }
 worker.shutdown();gpuSurface.shutdown();rendererOwner.reset();sceneOwner.reset();gfxExit();return 0;
}
