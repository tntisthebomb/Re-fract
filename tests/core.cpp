#include "engine.hpp"
#include "ui.hpp"
#include "batch_queue.hpp"
#include "framebuffer.hpp"
#include "trace_benchmark.hpp"
#include "distance_field.hpp"
#include <limits>
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
 for(const char* polynomial:{"x*x-y*y-z*z+cx","2*x*y+cy","2*x*z+cz"}){
  check(e.compile(polynomial,error)&&e.native>0,"polynomial bytecode selects native derivative path");
  for(int i=0;i<500;++i){Vec z{(i%31-15)*.173f,(i%43-21)*.119f,(i%47-23)*.127f};Dual a,b;
   bool av=e.evaluate(z,c,a),bv=e.evaluateGeneric(z,c,b);check(av==bv&&(!av||(a.value==b.value&&a.d==b.d)),"native expression matches interpreter values and all derivatives");
  }
  for(Vec extreme:{Vec{1e30f,1e30f,1e30f},Vec{std::numeric_limits<float>::infinity(),1,1},Vec{std::numeric_limits<float>::quiet_NaN(),1,1}}){Dual a,b;check(e.evaluate(extreme,c,a)==e.evaluateGeneric(extreme,c,b),"native expression preserves overflow rejection");}
 }
 check(e.compile(" 2 * x * y + cy ",error)&&e.native==2,"native expression accepts whitespace");
 check(e.compile("2*x*y+cz",error)&&e.native==0,"edited polynomial falls back safely");
 for(int width:{320,400,319}){std::vector<Color> source(width*H);for(int k=0;k<width*H;++k)source[k]={uint8_t(k*3),uint8_t(k*7),uint8_t(k*11)};
  std::vector<uint8_t> target(width*H*3+16,0xCD);convertFramebuffer(target.data()+8,source.data(),width);bool matches=true;
  for(int x=0;x<width;++x)for(int y=0;y<H;++y){auto c=source[y*width+x];int k=8+(x*H+H-1-y)*3;matches&=target[k]==c.b&&target[k+1]==c.g&&target[k+2]==c.r;}
  check(matches,"framebuffer conversion matches landscape-to-BGR mapping");for(int k=0;k<8;++k)check(target[k]==0xCD&&target[target.size()-1-k]==0xCD,"framebuffer transpose keeps guard bytes");
 }
 {std::array<std::atomic<int>,257> visits{};LoopQueue loop;
  auto body=[](int i,void* ctx){++(*static_cast<std::array<std::atomic<int>,257>*>(ctx))[i];};
  for(int count:{0,1,2,31,257}){for(auto& v:visits)v.store(0);loop.reset(count,body,&visits);std::thread worker([&](){loop.consume();});loop.consume();worker.join();
   for(int k=0;k<257;++k)check(visits[k].load()==(k<count?1:0),"parallel for executes each iteration exactly once");}
 }
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
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;int n=0;while(std::getline(in,line)){if(line.rfind("GPU ",0)==0||line.rfind("LIGHT ",0)==0||line.rfind("OPTICS ",0)==0||line.rfind("POINT ",0)==0)continue;if(n==0)out<<"REFRACT 1\n";else if(n!=2&&(n<5||n>7))out<<line<<'\n';++n;}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.settings.temporal&&!loaded.settings.customGradient,"legacy v1 file still loads");
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;int n=0;while(std::getline(in,line)){if(line.rfind("GPU ",0)==0||line.rfind("LIGHT ",0)==0||line.rfind("OPTICS ",0)==0||line.rfind("POINT ",0)==0)continue;if(n==0)out<<"REFRACT 2\n";else if(n!=2)out<<line<<'\n';++n;}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.formula.repeat&&loaded.settings.temporal,"legacy v2 file still loads");
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
 Formula repeated=preset(7);repeated.repeat=true;repeated.repeatPeriod={16,0,16};check(repeated.validate(error),"world repetition validates");
 for(int i=0;i<20;++i){Vec p{(i-10)*.125f,.25f,(i%7)*.25f};auto a=distance(repeated,p),b=distance(repeated,p+Vec{16,0,-32});check(a.valid==b.valid&&near(a.distance,b.distance,1e-5f)&&near(a.trap,b.trap,1e-4f),"periodic geometry and coloring repeat");}
 check(near(repeatBoundaryStep(repeated,{7,0,0},{1,0,0}),1)&&near(repeatBoundaryStep(repeated,{-7,0,0},{-1,0,0}),1),"marching respects both cell boundaries");
 check(repeatBoundaryStep(repeated,{0,0,0},{0,1,0})>1e20f,"zero spacing disables one repetition axis");
 s=Scene{};s.formula=repeated;check(saveScene(s,"test.rfs",error)&&loadScene(loaded,"test.rfs",error)&&loaded.formula.repeat&&near(loaded.formula.repeatPeriod.z,16),"world repetition scene roundtrip");
 repeated.repeatPeriod.x=.001f;check(!repeated.validate(error),"unsafe tiny repetition spacing rejected");
 // Specialized pipelines must preserve arbitrary parameter edits and stage semantics.
 for(int n=0;n<PresetCount;++n)for(int variant=0;variant<4;++variant){Formula f=preset(n);
  if(variant==1){f.julia=true;f.constant={.17f,-.21f,.09f};f.repeat=true;f.repeatPeriod={8,0,8};}
  if(variant==2){Stage ignored;ignored.kind=Kind::Offset;ignored.enabled=false;f.stages.insert(f.stages.begin(),ignored);f.terminal=1;}
  if(variant==3){Stage rotated;rotated.kind=Kind::Rotate;rotated.a=.13f;rotated.b=.21f;rotated.c=-.07f;f.stages.push_back(rotated);f.terminal=2;}
  check(f.validate(error),"edited pipeline validates");
  for(int i=0;i<120;++i){Vec p{(i%13-6)*.31f,(i%17-8)*.27f,(i%19-9)*.23f};Sample a=distance(f,p),b=distanceGeneric(f,p);
   check(a.valid==b.valid&&(!a.valid||(a.distance==b.distance&&a.trap==b.trap)),"specialized pipeline matches generic stage interpreter");
   a=distanceOnly(f,p);b=distanceGeneric(f,p,false);check(a.valid==b.valid&&(!a.valid||a.distance==b.distance),"specialized geometry-only pipeline matches generic");
  }
 }
 for(int n:{0,6,7,10,12}){s=Scene{};s.formula=preset(n);s.camera=presetCamera(n);s.settings.shadow=12;s.settings.ao=3;Scene generic=s;generic.formula.kernel=0;rays=Rays(s);
  for(int y=40;y<240;y+=60)for(int x=40;x<400;x+=60){auto a=trace(s,rays,x,y,0),b=trace(generic,rays,x,y,0);check(a.r==b.r&&a.g==b.g&&a.b==b.b,"specialized shaded pixels match generic exactly");}}
 // Material cache must reproduce a fresh shaded ray, including fog and exposure.
 s=Scene{};s.formula=preset(7);s.camera=presetCamera(7);s.settings.ao=2;s.settings.shadow=8;rays=Rays(s);
 for(int y=10;y<H;y+=30)for(int x=10;x<W;x+=30){auto r=renderJob(s,rays,{x,y,1,0,0,false});
  if(r.shade.valid){Scene edited=s;edited.settings.customGradient=true;edited.settings.gradientLow={.1f,.7f,.3f};edited.settings.gradientHigh={.8f,.2f,.9f};edited.settings.gradientRepeat=true;edited.settings.gradientOffset=.3f;edited.settings.exposure=1.7f;edited.settings.fog=.13f;
   auto a=recolorSample(edited.settings,r.shade),b=renderJob(edited,Rays(edited),{x,y,1,0,0,false}).color;
   check(a.r==b.r&&a.g==b.g&&a.b==b.b,"cached material matches fresh trace");}}
 s.settings.previewBlock=16;s.settings.autoRefine=false;renderer.invalidate(s,false);while(!renderer.complete())renderer.step(s,rays,0);
 auto cacheRays=renderer.rays();s.settings.palette=2;check(renderer.recolor(s)&&renderer.rays()==cacheRays,"completed cache recolors without tracing");
 renderer.invalidate(s,true);check(!renderer.recolor(s),"moving cache cannot recolor");
 // Exercise parallel material rows using the same two-consumer loop as native.
 s.settings.previewBlock=4;s.settings.autoRefine=false;Renderer parallelColor,serialColor;int loops=0;
 parallelColor.setParallelFor([](int n,LoopBody body,void* context,void* counter){++*static_cast<int*>(counter);LoopQueue q;q.reset(n,body,context);std::thread worker([&](){q.consume();});q.consume();worker.join();},&loops);
 parallelColor.invalidate(s,false);serialColor.invalidate(s,false);
 while(!parallelColor.complete())parallelColor.step(s,rays,1);
 while(!serialColor.complete())serialColor.step(s,rays,1);
 s.settings.gradientOffset=.27f;s.settings.exposure=1.31f;
 auto countBefore=parallelColor.rays();check(parallelColor.recolor(s)&&serialColor.recolor(s)&&loops==1&&parallelColor.rays()==countBefore,"parallel recoloring uses persistent-loop interface without ray tracing");
 for(int eye=0;eye<2;++eye){bool equal=true;for(int k=0;k<W*H;++k){auto a=parallelColor.image(eye)[k],b=serialColor.image(eye)[k];equal&=a.r==b.r&&a.g==b.g&&a.b==b.b;}check(equal,"parallel material rows match serial output");}
 // PICA's sideways projection must agree with CPU camera rays in both eyes.
 s=Scene{};s.camera.position={.7f,-.2f,-4};s.camera.yaw=.31f;s.camera.pitch=-.19f;rays=Rays(s);
 for(float eye:{-.04f,0.f,.04f})for(float x:{4.f,200.f,396.f})for(float y:{4.f,120.f,236.f}){
  Vec origin,direction;rays.ray(s,x,y,eye,origin,direction);Vec point=origin+direction*3;auto m=meshProjection(s,eye);float v[4]={};
  for(int row=0;row<4;++row)v[row]=m[row*4]*point.x+m[row*4+1]*point.y+m[row*4+2]*point.z+m[row*4+3];
  check(near(W*.5f*(1-v[1]/v[3]),x,.001f)&&near(H*.5f*(1-v[0]/v[3]),y,.001f),"GPU projection matches off-axis CPU stereo ray");
  check(v[2]/v[3]>=-1&&v[2]/v[3]<=0,"GPU projection uses PICA depth range");
 }
 std::vector<Color> meshColors(W*H,Color{128,64,32});std::vector<float> meshDepths(W*H,3);
 s=Scene{};s.settings.gpuCache=true;SurfaceMesh mesh=surfaceMesh(s,meshColors,meshDepths,1,0);
 check(mesh.vertices.size()==6000&&mesh.indices.size()==99*59*6,"surface cache produces bounded indexed grid");
 int meshLoops=0;SurfaceMesh parallelMesh=surfaceMesh(s,meshColors,meshDepths,1,0,[](int n,LoopBody body,void* ctx,void* counter){++*static_cast<int*>(counter);LoopQueue q;q.reset(n,body,ctx);std::thread worker([&](){q.consume();});q.consume();worker.join();},&meshLoops);
 bool identicalMesh=mesh.indices==parallelMesh.indices&&mesh.vertices.size()==parallelMesh.vertices.size();
 for(size_t i=0;i<mesh.vertices.size();++i){auto a=mesh.vertices[i],b=parallelMesh.vertices[i];identicalMesh&=a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&a.color.x==b.color.x&&a.color.y==b.color.y&&a.color.z==b.color.z;}
 check(meshLoops==1&&identicalMesh,"parallel surface rows preserve vertices and triangle topology exactly");
 {Scene far=s;far.camera.position={9999,-9999,9999};SurfaceMesh local=surfaceMesh(far,meshColors,meshDepths,1,0);bool same=true;
  for(size_t i=0;i<mesh.vertices.size();++i){auto a=mesh.vertices[i].position,b=local.vertices[i].position;same&=a.x==b.x&&a.y==b.y&&a.z==b.z;}
  check(same&&near(local.origin.x,9999),"GPU mesh local coordinates avoid precision loss at large world positions");
  auto matrix=meshProjection(far,0,local.origin),ordinary=meshProjection(s,0,s.camera.position);check(matrix==ordinary,"recentered GPU projection is independent of large world translation");
 }
 bool bounded=true;for(auto i:mesh.indices)bounded&=i<mesh.vertices.size();check(bounded,"mesh indices stay within 16-bit vertex bounds");
 for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(x>=100&&x<200)meshDepths[y*W+x]=0;
 mesh=surfaceMesh(s,meshColors,meshDepths,1,0);bool avoidsHoles=true;
 for(auto i:mesh.indices){int x=int(i%100)*4;avoidsHoles&=x<100||x>=200;}check(avoidsHoles,"mesh triangles exclude missed rays");
 for(int y=0;y<H;++y)for(int x=0;x<W;++x)meshDepths[y*W+x]=x<200?3:8;
 mesh=surfaceMesh(s,meshColors,meshDepths,1,0);bool avoidsJump=true;
 for(size_t k=0;k<mesh.indices.size();k+=3){int a=mesh.indices[k]%100,b=mesh.indices[k+1]%100,c=mesh.indices[k+2]%100;avoidsJump&=(a<50&&b<50&&c<50)||(a>=50&&b>=50&&c>=50);}
 check(avoidsJump,"mesh rejects triangles bridging depth discontinuities");
 check(surfaceMesh(s,{},meshDepths,1,0).indices.empty()&&surfaceMesh(s,meshColors,meshDepths,8,0).indices.empty(),"invalid buffers and too-coarse source reject capture");
 s.settings.meshStride=8;s.settings.meshEdge=.12f;check(saveScene(s,"test.rfs",error)&&loadScene(loaded,"test.rfs",error)&&loaded.settings.gpuCache&&loaded.settings.meshStride==8&&near(loaded.settings.meshEdge,.12f),"GPU controls persist in scene v4");
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;while(std::getline(in,line)){if(line.rfind("GPU ",0)==0||line.rfind("LIGHT ",0)==0||line.rfind("OPTICS ",0)==0||line.rfind("POINT ",0)==0)continue;if(line=="REFRACT 6")line="REFRACT 3";out<<line<<'\n';}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.settings.gpuCache,"legacy v3 defaults to CPU rendering");
 s=Scene{};s.settings.gpuCache=true;s.settings.previewBlock=4;s.settings.autoRefine=false;rays=Rays(s);renderer.invalidate(s,false);
 while(!renderer.complete())renderer.step(s,rays,0);
 check(renderer.captureSurface(s,mesh)&&!mesh.indices.empty(),"completed single-sample render captures geometry without tracing");
 renderer.invalidate(s,true);check(!renderer.captureSurface(s,mesh),"moving or unfinished render cannot capture stale mesh");
 for(int power:{2,4,8,16}){Formula f=preset(0);f.stages[0].a=float(power);f.algebraicBulb=true;f.iterations=1;check(f.validate(error),"algebraic bulb validates editable power");
  for(int i=0;i<300;++i){Vec p{(i%13-6)*.17f,(i%17-8)*.13f,(i%19-9)*.11f};auto a=distance(f,p),b=distanceGeneric(f,p),exact=distance(f,p,true);
   check(a.valid==b.valid&&(!a.valid||(near(a.distance,b.distance,2e-5f*std::fmax(1.f,b.distance))&&near(a.trap,b.trap,2e-4f*std::fmax(1.f,b.trap)))),"single algebraic power agrees with trigonometric transform within relative float tolerance");
   check(exact.valid==b.valid&&exact.distance==b.distance&&exact.trap==b.trap,"exact distance bypasses algebraic bulb approximation");
  }
 }
 s=Scene{};s.formula.algebraicBulb=true;s.settings.quality=true;rays=Rays(s);Scene exactBulb=s;exactBulb.formula.algebraicBulb=false;
 for(int y=40;y<H;y+=60)for(int x=40;x<W;x+=60){auto a=trace(s,rays,x,y,0),b=trace(exactBulb,rays,x,y,0);check(a.r==b.r&&a.g==b.g&&a.b==b.b,"quality render retains exact bulb math with experiment enabled");}
 {Scene cacheScene;cacheScene.camera.position={0,0,-4};cacheScene.settings.fieldSpacing=.25f;
  auto grid=std::make_shared<DistanceField>(cacheScene);float estimate=0;
  check(!grid->lookup({0,0,-4},estimate),"incomplete distance field bypasses approximate lookups");
  while(!grid->ready())grid->build();
  check(grid->lookup({0,0,-4},estimate)&&estimate>0,"completed field caches open space");
  check(!grid->lookup({0,0,0},estimate)&&!grid->lookup({100,0,0},estimate),"field rejects near geometry and out-of-domain points");
  check(!grid->lookup({std::numeric_limits<float>::quiet_NaN(),0,0},estimate),"distance field rejects nonfinite lookup coordinates");
  check(grid->containsCamera({0,0,-4})&&!grid->containsCamera({100,0,0}),"camera movement bounds grid reuse");
  TraceBenchmark cachedPreview(cacheScene,grid,true),exactPreview(cacheScene,{},true);
  while(!cachedPreview.complete()){cachedPreview.step();exactPreview.step();}
  check(cachedPreview.profile().reused>0&&cachedPreview.profile().distanceQueries<exactPreview.profile().distanceQueries,"open-space field preview replaces formula evaluations");
  Rays cacheRays(cacheScene);cacheRays.field=grid.get();cacheScene.settings.quality=true;
  auto cached=renderJob(cacheScene,cacheRays,{200,120,1,0,0,true});cacheRays.field=nullptr;auto exact=renderJob(cacheScene,cacheRays,{200,120,1,0,0,true});
  check(cached.color.r==exact.color.r&&cached.color.g==exact.color.g&&cached.color.b==exact.color.b&&cached.depth==exact.depth,"quality render bypasses approximate field");
 }
 // Zoom precision retains the legacy ceiling and shrinks with pixel footprint.
 s=Scene{};float legacyTolerance=hitTolerance(s.settings,.01f,.5f);
 check(near(legacyTolerance,.002f,1e-8f),"legacy near-hit tolerance unchanged");
 s.settings.adaptivePrecision=true;
 check(hitTolerance(s.settings,.01f,.5f)<legacyTolerance*.01f,"zoom precision resolves finer near-surface detail");
 check(hitTolerance(s.settings,0,.5f)==s.settings.minEpsilon,"zoom precision has a finite floor");
 check(hitTolerance(s.settings,100,.5f)<=.040001f,"zoom precision respects legacy distant ceiling");
 s.settings.adaptiveDetail=true;s.camera.position={0,0,0};
 check(renderingIterations(s,true)==8&&renderingIterations(s,false)==24,"adaptive detail reduces moving work and increases close detail");
 check(s.formula.iterations==9,"adaptive detail preserves editable formula iteration count");
 s.settings.detailIterations=3;check(renderingIterations(s,false)>=9,"detail cap never reduces stationary base iterations");
 s=Scene{};rays=Rays(s);RenderJob bounceJob{200,120,1,0,0,false};
 auto unlit=renderJob(s,rays,bounceJob);check(unlit.hit,"indirect-light reference ray hits geometry");
 s.settings.giSamples=4;s.settings.giSteps=32;s.settings.giRange=.5f;
 auto indirect=renderJob(s,rays,bounceJob),repeatIndirect=renderJob(s,rays,bounceJob);
 check(indirect.depth==unlit.depth&&indirect.profile.distanceQueries>unlit.profile.distanceQueries,"indirect lighting preserves geometry and records secondary work");
 check(indirect.color.r==repeatIndirect.color.r&&indirect.color.g==repeatIndirect.color.g&&indirect.color.b==repeatIndirect.color.b,"indirect sampling deterministic across frames");
 auto cachedIndirect=recolorSample(s.settings,indirect.shade);
 check(cachedIndirect.r==indirect.color.r&&cachedIndirect.g==indirect.color.g&&cachedIndirect.b==indirect.color.b,"shade cache stores indirect RGB contribution");
 s.settings.giStrength=0;auto disabledIndirect=renderJob(s,rays,bounceJob);
 check(disabledIndirect.profile.distanceQueries==unlit.profile.distanceQueries&&disabledIndirect.color.r==unlit.color.r&&disabledIndirect.color.g==unlit.color.g&&disabledIndirect.color.b==unlit.color.b,"zero indirect strength bypasses extra queries");
 s.settings.giStrength=.7f;s.settings.adaptivePrecision=true;s.settings.gpuAutoRefresh=true;s.settings.adaptiveDetail=true;
 s.settings.distanceField=true;s.settings.fieldSpacing=.4f;s.settings.minEpsilon=.000002f;s.settings.skyColor={.2f,.3f,.4f};
 check(saveScene(s,"test.rfs",error)&&loadScene(loaded,"test.rfs",error)&&loaded.settings.distanceField&&near(loaded.settings.fieldSpacing,.4f)&&loaded.settings.adaptivePrecision&&loaded.settings.adaptiveDetail&&loaded.settings.gpuAutoRefresh&&loaded.settings.giSamples==4&&near(loaded.settings.skyColor.z,.4f),"v5 precision, indirect and refresh settings roundtrip");
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;while(std::getline(in,line)){if(line.rfind("LIGHT ",0)==0||line.rfind("OPTICS ",0)==0||line.rfind("POINT ",0)==0)continue;if(line=="REFRACT 6")line="REFRACT 4";out<<line<<'\n';}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.settings.adaptivePrecision&&loaded.settings.giSamples==0,"legacy v4 uses original precision and no indirect lighting");
 s.settings.giSamples=5;check(!saveScene(s,"bad.rfs",error),"reject excessive secondary samples");
 s.settings.giSamples=0;s.settings.minEpsilon=0;check(!saveScene(s,"bad.rfs",error),"reject zero precision floor");
 s=Scene{};TraceBenchmark serialBench(s),parallelBench(s);
 auto parallelBenchmark=[](const Scene& s,const Rays& rays,const RenderJob* jobs,RenderResult* results,int count,void*){BatchQueue q;q.reset(s,rays,jobs,results,count);std::thread thread([&](){q.consume();});q.consume();thread.join();};
 while(!serialBench.complete()){serialBench.step();parallelBench.step(parallelBenchmark);}
 check(serialBench.digest()==parallelBench.digest()&&serialBench.profile().distanceQueries==parallelBench.profile().distanceQueries,"frozen-view benchmark serial and parallel digests match");
 check(serialBench.completedSamples()==1500&&serialBench.profile().rays==1500&&serialBench.raysPerSecond()==0,"benchmark workload bounded and zero elapsed safe");
 serialBench.recordMs(100);check(near(serialBench.raysPerSecond(),15000,.01f),"benchmark rate measures traced rays per active second");
 for(int n=24;n<PresetCount;++n){Scene corridor;corridor.formula=preset(n);corridor.camera=presetCamera(n);corridor.settings=presetSettings(n);check(saveScene(corridor,"test.rfs",error),"new preset appearance and camera save within supported range");}
 // Thin-lens rays converge at the forward-depth focus plane for both stereo eyes.
 s=Scene{};s.settings.aperture=.2f;s.settings.focusDistance=4;s.settings.convergence=4;rays=Rays(s);
 for(float eye:{-.04f,0.f,.04f})for(float x:{10.f,200.f,390.f}){
  Vec origin,direction;rays.ray(s,x,95,eye,origin,direction);Vec target=origin+direction*(4/direction.dot(rays.forward));
  for(Vec lens:{Vec{0,0,0},Vec{.5f,.3f,0},Vec{-.4f,.6f,0}}){Vec lo,ld;rays.lensRay(s,x,95,eye,lens.x,lens.y,lo,ld);
   Vec focus=lo+ld*(4/ld.dot(rays.forward));check((focus-target).length()<.00001f,"lens rays retain the stereo focus-plane target");check(near(ld.length(),1,1e-6f),"lens rays normalized");}
 }
 s.settings.aperture=0;Vec lo,ld,po,pd;rays.lensRay(s,120,80,.04f,.8f,-.2f,lo,ld);rays.ray(s,120,80,.04f,po,pd);
 check(lo.x==po.x&&lo.y==po.y&&lo.z==po.z&&ld.x==pd.x&&ld.y==pd.y&&ld.z==pd.z,"zero aperture preserves pinhole ray exactly");
 // Camera-relative lights use camera basis; world lights stay fixed.
 s=Scene{};s.camera.yaw=.7f;s.camera.pitch=.3f;s.settings.pointLights[0].enabled=true;s.settings.pointLights[0].position={1,2,3};rays=Rays(s);
 Vec expectedLamp=s.camera.position+rays.right+rays.up*2+rays.forward*3;
 check((rays.pointPositions[0]-expectedLamp).length()<1e-6f&&rays.pointMask==1,"camera light resolves position once per view");
 s.settings.pointLights[0].cameraRelative=false;rays=Rays(s);check((rays.pointPositions[0]-Vec{1,2,3}).length()==0,"world point light does not follow camera");
 // Point lights change shading without changing primary hits; cached colors stay consistent.
 s=Scene{};rays=Rays(s);RenderJob opticalJob{200,120,1,0,0,false};auto pinhole=renderJob(s,rays,opticalJob);
 s.settings.pointLights[0].enabled=true;s.settings.pointLights[0].intensity=15;s.settings.pointLights[0].position={0,0,0};rays=Rays(s);
 auto point=renderJob(s,rays,opticalJob);auto pointCached=recolorSample(s.settings,point.shade);
 check(point.hit&&point.depth==pinhole.depth&&point.shade.localDiffuse.length()>0,"point lighting preserves geometry and illuminates visible surface");
 s.settings.pointLights[0].intensity=50;s.settings.exposure=4;auto brightPoint=renderJob(s,Rays(s),opticalJob);
 check(brightPoint.radiance.x>1&&brightPoint.color.r==255,"point-light samples retain HDR radiance above display clipping");s.settings.pointLights[0].intensity=15;s.settings.exposure=1;
 check(pointCached.r==point.color.r&&pointCached.g==point.color.g&&pointCached.b==point.color.b,"point-light RGB retained by recoloring cache");
 s.settings.pointLights[0].range=.1f;auto beyondRange=renderJob(s,Rays(s),opticalJob);
 check(beyondRange.color.r==pinhole.color.r&&beyondRange.color.g==pinhole.color.g&&beyondRange.color.b==pinhole.color.b,"finite point light range excludes distant surfaces");
 s.settings.pointLights[0].range=8;s.settings.shadow=32;s.settings.pointShadows=false;auto unshadowedPoint=renderJob(s,Rays(s),opticalJob);s.settings.pointShadows=true;auto shadowedPoint=renderJob(s,Rays(s),opticalJob);
 check(shadowedPoint.profile.distanceQueries>unshadowedPoint.profile.distanceQueries,"point shadows account for bounded extra geometry queries");
 s=Scene{};s.settings.dof=true;s.settings.aperture=.15f;s.settings.dofSamples=8;rays=Rays(s);
 auto lens=renderJob(s,rays,opticalJob);opticalJob.moving=true;auto movingLens=renderJob(s,rays,opticalJob);
 check(lens.profile.rays==8&&!lens.shade.valid&&movingLens.profile.rays==1,"lens sampling is stationary-only and disables single-ray geometry cache");
 opticalJob.moving=false;auto sameLens=renderJob(s,rays,opticalJob);
 check(lens.color.r==sameLens.color.r&&lens.color.g==sameLens.color.g&&lens.color.b==sameLens.color.b,"lens sample sequence reproducible");
 // HDR accumulation averages completed lighting passes, with no stale motion/stereo samples.
 s=Scene{};s.settings.previewBlock=16;s.settings.autoRefine=false;s.settings.giSamples=1;s.settings.progressiveLighting=true;s.settings.lightingPasses=3;s.settings.gpuCache=true;s.settings.temporal=true;
 rays=Rays(s);Renderer progressive;progressive.beginFrame(s,false,true,1);int progressiveSteps=0;
 while(!progressive.complete()&&progressiveSteps++<2000)progressive.step(s,rays,1);
 check(progressive.complete()&&progressive.accumulatedPasses()==3&&progressive.rays()==uint64_t(25*15*2*3),"progressive pass scheduler covers each stereo cell exactly once per pass");
 Vec sum;for(int pass=0;pass<3;++pass)sum=sum+renderJob(s,rays,{192,112,16,0,-s.settings.eyeSeparation*.5f,false,false,uint32_t(pass),true}).radiance;
 Color mean{uint8_t(clamp(sum.x/3,0,1)*255),uint8_t(clamp(sum.y/3,0,1)*255),uint8_t(clamp(sum.z/3,0,1)*255)};auto accumulated=progressive.image(0)[112*W+192];
 check(std::abs(int(mean.r)-accumulated.r)<=1&&std::abs(int(mean.g)-accumulated.g)<=1&&std::abs(int(mean.b)-accumulated.b)<=1,"progressive output matches unquantized sample average");
 Renderer parallelProgressive;parallelProgressive.setBatchShader(parallelBenchmark,nullptr);parallelProgressive.invalidate(s,false);
 while(!parallelProgressive.complete())parallelProgressive.step(s,rays,1);
 bool sameAccumulation=true;for(int eye=0;eye<2;++eye)for(int k=0;k<W*H;++k){auto a=progressive.image(eye)[k],b=parallelProgressive.image(eye)[k];sameAccumulation&=a.r==b.r&&a.g==b.g&&a.b==b.b;}
 check(sameAccumulation,"parallel progressive jobs preserve deterministic stereo accumulation");
 check(!progressive.recolor(s)&&!progressive.captureSurface(s,mesh),"accumulated image cannot masquerade as a single geometric surface capture");
 progressive.beginFrame(s,true,false,1);progressive.step(s,rays,1);
 check(progressive.profile().reused==0,"accumulated samples do not seed temporal surface reuse on motion");
 progressive.beginFrame(s,false,false,0);check(!progressive.complete()&&progressive.accumulatedPasses()==0,"stereo change resets progressive accumulation without relying on UI motion");
 progressive.invalidate(s,true);check(progressive.accumulatedPasses()==0,"movement clears accumulated pass count");
 s.settings.lightingPasses=1;progressive.invalidate(s,false);while(!progressive.complete())progressive.step(s,rays,0);
 auto restarted=renderJob(s,rays,{192,112,16,0,0,false,false,0,true}).color,afterReset=progressive.image(0)[112*W+192];
 check(restarted.r==afterReset.r&&restarted.g==afterReset.g&&restarted.b==afterReset.b,"restart replaces previous sums instead of blending old viewpoint");
 s.settings.gpuCache=true;s.settings.dof=true;s.settings.dofSamples=2;s.settings.pointLights[1].enabled=true;s.settings.pointLights[1].cameraRelative=false;s.settings.pointLights[1].color={.1f,.3f,.9f};s.settings.pointShadows=true;s.settings.sunStrength=.4f;
 check(saveScene(s,"test.rfs",error)&&loadScene(loaded,"test.rfs",error)&&loaded.settings.dof&&loaded.settings.dofSamples==2&&loaded.settings.progressiveLighting&&loaded.settings.pointLights[1].enabled&&!loaded.settings.pointLights[1].cameraRelative&&near(loaded.settings.pointLights[1].color.z,.9f)&&near(loaded.settings.sunStrength,.4f),"v6 lights, lens and progressive settings roundtrip");
 {std::ifstream in("test.rfs");std::ofstream out("bad.rfs");std::string line;while(std::getline(in,line)){if(line.rfind("OPTICS ",0)==0||line.rfind("POINT ",0)==0)continue;if(line=="REFRACT 6")line="REFRACT 5";out<<line<<'\n';}}
 check(loadScene(loaded,"bad.rfs",error)&&!loaded.settings.dof&&!loaded.settings.progressiveLighting&&!loaded.settings.pointLights[0].enabled&&!loaded.settings.pointLights[1].enabled,"legacy v5 defaults to pinhole, fixed lighting and disabled point lights");
 s.settings.focusDistance=0;check(!saveScene(s,"bad.rfs",error),"reject zero focal distance");s.settings.focusDistance=4;s.settings.lightingPasses=129;check(!saveScene(s,"bad.rfs",error),"reject unbounded accumulation passes");
 s.settings.lightingPasses=32;s.settings.pointLights[0].color.x=2;check(!saveScene(s,"bad.rfs",error),"reject invalid point-light color");
 if(argc>1){
  std::string prefix=argv[1];s=Scene{};s.settings.previewBlock=4;s.settings.autoRefine=false;
  for(int n=0;n<PresetCount;++n){s.settings=presetSettings(n);s.settings.previewBlock=4;s.settings.autoRefine=false;s.formula=preset(n);s.camera=presetCamera(n);s.settings.farClip=40;s.settings.convergence=-s.camera.position.z;rays=Rays(s);renderer.invalidate(s,false);
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
