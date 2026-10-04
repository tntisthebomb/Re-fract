#include "engine.hpp"
#include "ui.hpp"
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
