#include "engine.hpp"
#include "ui.hpp"
#include "batch_queue.hpp"
#include <thread>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <chrono>

using namespace rf;
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
bool near(float a,float b,float e=.002f){return std::fabs(a-b)<e;}
}
int main(int argc,char** argv){
 std::string error;Expression e;Dual d;
 check(e.compile("-2^2+pow(3,2)*2",error),"precedence compile");
 check(e.evaluate({}, {},d)&&near(d.value,14),"precedence and unary minus");
 check(e.compile("2^3^2",error)&&e.evaluate({}, {},d)&&near(d.value,512),"right associative power");
 check(e.compile("sin(x)*cos(y)+z^3+cx*cy-cz/2",error),"gradient compile");
 Vec z{.4f,.7f,.9f},c{.3f,.6f,.8f};check(e.evaluate(z,c,d),"gradient evaluate");
 float vars[]={z.x,z.y,z.z,c.x,c.y,c.z};
 for(int i=0;i<6;++i){float before=vars[i];vars[i]=before+.001f;Dual plus,minus;
  check(e.evaluate({vars[0],vars[1],vars[2]},{vars[3],vars[4],vars[5]},plus),"gradient plus");vars[i]=before-.001f;
  check(e.evaluate({vars[0],vars[1],vars[2]},{vars[3],vars[4],vars[5]},minus),"gradient minus");vars[i]=before;
  check(near(d.d[i],(plus.value-minus.value)/.002f,.005f),"AD matches finite differences");
 }
 for(const char* bad:{"","x+","unknown(x)","sin(x,y)","1e999","x y","pow(x)","(x"})check(!e.compile(bad,error),"reject malformed expression");
 check(!e.compile(std::string(100,'-')+"x",error),"bounded recursion");
 check(!e.compile(std::string(30,'(')+"x"+std::string(30,')'),error),"bounded parenthesis recursion");
 check(e.compile("x/0",error)&&!e.evaluate(z,c,d),"division domain");
 check(e.compile("sqrt(-1)",error)&&!e.evaluate(z,c,d),"sqrt domain");
 check(e.compile("log(0)",error)&&!e.evaluate(z,c,d),"log domain");
 check(e.compile("(-2)^3",error)&&e.evaluate(z,c,d)&&near(d.value,-8),"negative integer powers");
 check(e.compile("sin(pi/2)+2*3+x",error)&&e.count==3&&e.evaluate({4,0,0},{},d)&&near(d.value,11)&&near(d.d[0],1),"constant folding preserves value and derivative");
 check(e.compile("(-2)^x",error)&&!e.evaluate(z,c,d),"negative variable powers rejected");
 for(int i=0;i<PresetCount;++i){Formula f=preset(i);check(f.validate(error),"preset validates");
  for(int j=0;j<20;++j){Sample s=distance(f,{float(j-10)*.17f,float(j%5)*.19f,float(j%7)*.15f});
   check(s.valid&&std::isfinite(s.distance)&&s.distance>=0&&std::isfinite(s.trap),"preset DE finite and nonnegative");
  }
 }
 Scene s;s.formula=preset(16);s.formula.stages[0].text[0]="sin(x)*2 + cx";check(s.formula.validate(error),"custom formula validates");
 check(saveScene(s,"test.rfs",error),"save scene");Scene loaded;
 check(loadScene(loaded,"test.rfs",error),"load scene");check(loaded.formula.stages[0].text[0]==s.formula.stages[0].text[0],"expression roundtrip");
 check(loaded.settings.steps==s.settings.steps&&near(loaded.settings.eyeSeparation,s.settings.eyeSeparation),"settings roundtrip");
 {std::ofstream out("bad.rfs");out<<"REFRACT 1\n";}
 check(!loadScene(loaded,"bad.rfs",error),"reject truncated file");check(loaded.formula.stages[0].text[0]==s.formula.stages[0].text[0],"failed load leaves scene unchanged");
 {std::ofstream out("bad.rfs");out<<std::string(20000,'x');}
 check(!loadScene(loaded,"bad.rfs",error),"file size bound");
 s=Scene{};Rays rays(s);Vec ol,dl,orr,drr;rays.ray(s,200,120,-.04f,ol,dl);rays.ray(s,200,120,.04f,orr,drr);
 check(near(ol.x,-.04f)&&near(orr.x,.04f),"parallel eye baseline");
 check(near(ol.x+dl.x/dl.z*4,0)&&near(orr.x+drr.x/drr.z*4,0),"off-axis convergence plane");
 check(near(dl.length(),1)&&near(drr.length(),1),"normalized stereo rays");
 Renderer renderer;s.settings.previewBlock=16;s.settings.autoRefine=false;
 renderer.invalidate(s,false);int tasks=0;while(!renderer.complete()&&tasks<1000){renderer.step(s,rays,1);++tasks;}
 check(renderer.complete(),"interlaced pass completes");
 check(renderer.rays()==uint64_t(25*15*2),"one sample per coarse cell per eye");
 bool differs=false;int lit=0;for(int i=0;i<W*H;++i){auto a=renderer.image(0)[i],b=renderer.image(1)[i];differs|=a.r!=b.r||a.g!=b.g||a.b!=b.b;lit+=a.r>20;}
 check(differs,"left and right are independently rendered");check(lit>100,"default fractal visible");
 renderer.invalidate(s,false);while(!renderer.complete())renderer.step(s,rays,0);
 bool same=true;for(int i=0;i<W*H;++i){auto a=renderer.image(0)[i],b=renderer.image(1)[i];same&=a.r==b.r&&a.g==b.g&&a.b==b.b;}
 check(same,"slider zero gives identical eyes");
 // Refinement can be checked cheaply against an empty scene with one march step domain miss.
 s.settings.previewBlock=16;s.settings.autoRefine=true;s.settings.steps=8;s.settings.farClip=1;s.camera.position={0,0,-20};
 rays=Rays(s);renderer.invalidate(s,false);tasks=0;
 while(!renderer.complete()&&tasks<200000){renderer.step(s,rays,0);++tasks;}
 check(renderer.complete()&&renderer.currentBlock()==1,"progressive refinement reaches full resolution");
 // Geometry-only evaluation must preserve distance and validity across every preset.
 for(int n=0;n<PresetCount;++n){Formula f=preset(n);for(int i=0;i<40;++i){Vec p{(i-20)*.13f,(i%9-4)*.21f,(i%11-5)*.17f};auto a=distance(f,p),b=distanceOnly(f,p);check(a.valid==b.valid&&near(a.distance,b.distance,1e-6f),"distance-only agrees with coloring path");}}
 s=Scene{};s.camera.position={0,0,-20};check(near(cameraSpeedScale(s),1),"full speed far from surface");
 s.camera.position={0,0,0};check(near(cameraSpeedScale(s),s.camera.minimumSpeed,1e-6f),"minimum speed at surface");
 s.camera.position={0,0,-2};float first=cameraSpeedScale(s);s.camera.surfaceRange=4;check(cameraSpeedScale(s)<=first,"larger slowdown range reduces movement");
 s.camera.surfaceSpeed=false;check(near(cameraSpeedScale(s),1),"surface slowdown disabled");
 s.settings.customGradient=true;s.settings.gradientLow={1,0,0};s.settings.gradientHigh={0,0,1};s.settings.gradientScale=1;
 Vec color=gradientColor(s.settings,.25f);check(near(color.x,.75f)&&near(color.z,.25f),"custom gradient interpolation");
 color=gradientColor(s.settings,2);check(near(color.x,0)&&near(color.z,1),"gradient endpoint clamp");
 s.settings.gradientRepeat=true;color=gradientColor(s.settings,1.25f);check(near(color.x,.75f)&&near(color.z,.25f),"repeated gradient");
 s.settings.gradientOffset=-.5f;color=gradientColor(s.settings,.25f);check(near(color.x,.25f)&&near(color.z,.75f),"negative gradient phase wraps");
 s.settings.temporal=true;s.settings.batchSize=16;s.camera.minimumSpeed=.023f;
 check(saveScene(s,"test.rfs",error)&&loadScene(loaded,"test.rfs",error),"v2 scene roundtrip");
 check(loaded.settings.temporal&&loaded.settings.batchSize==16&&loaded.settings.customGradient&&near(loaded.settings.gradientLow.x,1)&&near(loaded.camera.minimumSpeed,.023f),"new settings persist");
 // Remove only the three v2 extension lines to obtain a legacy v1 scene.
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;int n=0;while(std::getline(in,line)){if(n==0)out<<"REFRACT 1\n";else if(n<4||n>6)out<<line<<'\n';++n;}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.settings.temporal&&!loaded.settings.customGradient,"legacy v1 file still loads");
 s=Scene{};rays=Rays(s);
 for(float eye:{-.04f,0.f,.04f})for(float x:{10.f,200.f,390.f}){Vec o,direction;rays.ray(s,x,95,eye,o,direction);float px,py,depth;
 check(rays.project(s,o+direction*3,eye,px,py,depth)&&near(px,x)&&near(py,95)&&near(depth,3),"stereo projection inverse");}
 RenderJob job{195,115,8,0,0,false};auto full=renderJob(s,rays,job);job.fast=true;auto fast=renderJob(s,rays,job);
 check(full.hit&&fast.hit&&near(full.depth,fast.depth),"fast lighting preserves geometry");check(fast.profile.shadingQueries<full.profile.shadingQueries,"fast lighting saves distance queries");
 s.settings.quality=true;auto qualityFast=renderJob(s,rays,job);job.fast=false;auto qualityFull=renderJob(s,rays,job);
 check(qualityFast.color.r==qualityFull.color.r&&qualityFast.profile.distanceQueries==qualityFull.profile.distanceQueries,"quality ignores preview lighting");
 s=Scene{};s.settings.previewBlock=16;s.settings.autoRefine=false;s.settings.batchSize=16;rays=Rays(s);
 Renderer batched;int calls=0;batched.setBatchShader([](const Scene& sc,const Rays& r,const RenderJob* j,RenderResult* out,int n,void* ctx){++*static_cast<int*>(ctx);int split=n/2;renderJobs(sc,r,j,out,split);renderJobs(sc,r,j+split,out+split,n-split);},&calls);
 batched.invalidate(s,false);renderer.invalidate(s,false);while(!batched.complete())batched.step(s,rays,1);while(!renderer.complete())renderer.step(s,rays,1);
 bool equal=true;for(int eye=0;eye<2;++eye)for(int k=0;k<W*H;++k){Color a=batched.image(eye)[k],b=renderer.image(eye)[k];equal&=a.r==b.r&&a.g==b.g&&a.b==b.b;}
 check(calls>0&&calls<375&&equal,"batched callback matches serial rendering with fewer handoffs");
 s.settings.temporal=true;s.settings.previewLighting=false;renderer.beginFrame(s,true,true,1);for(int i=0;i<150;++i)renderer.step(s,rays,1);
 renderer.resetProfile();renderer.beginFrame(s,true,false,1);for(int i=0;i<150;++i)renderer.step(s,rays,1);check(renderer.profile().reused>0,"temporal history reuses traced surfaces");
 renderer.resetProfile();renderer.beginFrame(s,true,true,1);renderer.step(s,rays,1);check(renderer.profile().reused==0,"scene changes invalidate history");
 s.settings.quality=true;s.settings.adaptiveTiles=true;s.settings.foveated=true;s.settings.relaxation=1.5f;
 renderer.resetProfile();renderer.beginFrame(s,false,true,1);renderer.step(s,Rays(s),1);check(renderer.profile().reused==0&&renderer.profile().skipped==0&&renderer.profile().relaxFallbacks==0,"quality bypasses experiments");
 StereoSlider sliderState;Settings stereoSettings;
 check(!sliderState.update(stereoSettings,0)&&sliderState.strength==0,"physical 2D stop stays mono");
 check(!sliderState.update(stereoSettings,.018f)&&sliderState.strength==0,"slider noise near 2D ignored");
 check(sliderState.update(stereoSettings,.1f)&&sliderState.strength>0,"slider activates stereo beyond dead zone");
 float stable=sliderState.strength;check(!sliderState.update(stereoSettings,stable+.005f)&&sliderState.strength==stable,"minor slider jitter does not reset view");
 check(sliderState.update(stereoSettings,0)&&sliderState.strength==0,"slider returns to mono");
 stereoSettings.stereo=false;check(!sliderState.update(stereoSettings,1)&&sliderState.strength==0,"disabled stereo ignores slider");
 stereoSettings.stereo=true;stereoSettings.eyeSeparation=0;check(!sliderState.update(stereoSettings,1),"zero separation ignores slider");
 s=Scene{};s.settings.previewBlock=16;s.settings.autoRefine=false;rays=Rays(s);Renderer monoEnabled,monoDisabled;
 monoEnabled.beginFrame(s,false,true,0);while(!monoEnabled.complete())monoEnabled.step(s,rays,0);
 s.settings.stereo=false;monoDisabled.beginFrame(s,false,true,0);while(!monoDisabled.complete())monoDisabled.step(s,rays,0);
 check(monoEnabled.rays()==375&&monoEnabled.rays()==monoDisabled.rays()&&monoEnabled.profile().batches==monoDisabled.profile().batches,"stereo enabled at 2D has same render work as disabled");
 auto rev=monoEnabled.imageRevision();monoEnabled.beginFrame(s,false,false,0);monoEnabled.step(s,rays,0);
 check(monoEnabled.complete()&&monoEnabled.imageRevision()==rev,"inactive slider leaves completed render untouched");
 check(&monoEnabled.image(0)==&monoEnabled.image(1),"mono export shares image without duplicate writes");
 // Exercise the exact native work-claim algorithm with real concurrent consumers.
 s=Scene{};rays=Rays(s);RenderJob queueJobs[32];RenderResult serialResults[32],parallelResults[32];
 for(int i=0;i<32;++i)queueJobs[i]={i*12,110,8,i%2,(i%2?1.f:-1.f)*.03f,false};
 renderJobs(s,rays,queueJobs,serialResults,32);BatchQueue queue;
 for(int repeat=0;repeat<12;++repeat){queue.reset(s,rays,queueJobs,parallelResults,32);std::thread workerThread([&](){queue.consume();});queue.consume();workerThread.join();
  for(int i=0;i<32;++i){const auto& a=serialResults[i];const auto& b=parallelResults[i];check(a.color.r==b.color.r&&a.color.g==b.color.g&&a.color.b==b.color.b&&a.hit==b.hit&&near(a.depth,b.depth,1e-6f)&&a.profile.distanceQueries==b.profile.distanceQueries,"shared queue matches serial ray results");}}
 if(argc>1){
  std::string prefix=argv[1];s=Scene{};s.settings.previewBlock=4;s.settings.autoRefine=false;
  for(int n=0;n<PresetCount;++n){s.formula=preset(n);s.camera=presetCamera(n);s.settings.farClip=40;s.settings.convergence=-s.camera.position.z;rays=Rays(s);renderer.invalidate(s,false);
   auto start=std::chrono::steady_clock::now();while(!renderer.complete())renderer.step(s,rays,1);
   check(savePPM(renderer.image(0),prefix+"-"+std::to_string(n)+".ppm",error),"render export");
   auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
   std::cout<<presetName(n)<<": "<<ms<<" ms (desktop; not a 3DS benchmark)\n";
  }
  Canvas panel(320,240);drawPanel(panel,3,3,{{"STAGE INDEX","1"},{"OPERATION","BULB POWER"},{"ENABLED","ON"},{"POWER","8"},{"THETA MULTIPLIER","1"},{"PHI MULTIPLIER","1"},{"MOVE EARLIER","A REORDER"}},"PAD MOVE C-STICK LOOK ZL/ZR UP/DOWN","MANDELBULB / 8",4,.42f,false,8);
  std::ofstream ui(prefix+"-ui.ppm",std::ios::binary);ui<<"P6\n320 240\n255\n";for(Color c:panel.pixels){char bytes[]={char(c.r),char(c.g),char(c.b)};ui.write(bytes,3);}
 }
 std::remove("test.rfs");std::remove("bad.rfs");std::cout<<"PASS: "<<checks<<" checks\n";
}
