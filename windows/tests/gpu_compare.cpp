// GPU vs CPU comparison for the OpenGL ray marcher.
// Build (Linux, from windows/):
//   g++ -std=c++17 -O2 -pthread -Iengine -Isrc $(pkg-config --cflags sdl2) tests/gpu_compare.cpp src/gpu_renderer.cpp \
//       src/gl_loader.cpp engine/renderer.cpp engine/formula.cpp engine/expression.cpp engine/surface.cpp engine/upscale.cpp \
//       $(pkg-config --libs sdl2) -lGL -o /tmp/gpu_compare
// Run: LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 xvfb-run -a /tmp/gpu_compare [name-prefix] [dump-prefix]
//   name-prefix filters cases (substring match, "" = all); dump-prefix writes <prefix>_<n>.ppm CPU|GPU side-by-sides.
// Reference = rf::renderJob() per pixel at full resolution (still quality, block 1). Stochastic features (lens, GI
// with >4 samples) use the same deterministic Halton sequences as the CPU, so they match closely, not just in the mean.
#include "gpu_renderer.hpp"
#include "gl_loader.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using namespace rf;

struct Case {const char* name;int preset;void (*tweak)(Scene&);};
static void none(Scene&){}
static void repeatOn(Scene& s){s.formula.repeat=true;s.formula.repeatPeriod={6,6,6};s.camera.position={0,0,-2.5f};}
static void quality(Scene& s){s.settings.quality=true;s.settings.samples=4;}
static void quality6(Scene& s){s.settings.quality=true;s.settings.samples=6;s.settings.steps=96;}
static void aoShadow(Scene& s){s.settings.ao=3;s.settings.shadow=24;s.settings.specular=.4f;}
static void aoShadowHeavy(Scene& s){s.settings.ao=32;s.settings.shadow=512;s.settings.specular=.4f;}
static void algebraic(Scene& s){s.formula.algebraicBulb=true;}
static void exprOps(Scene& s){auto& st=s.formula.stages[0];
 st.text={{"x*x-y*y-z*z+cx+0.05*sin(3*y)","2*x*y+cy+0.1*abs(z)*min(x,0.3)","2*x*z+cz-0.05*sqrt(x*x+1)+0.02*pow(y,2)/(1+exp(-z))+0.01*log(x*x+2)*cos(x)"}};
 std::string e;s.formula.validate(e);if(!e.empty())std::printf("expr err %s\n",e.c_str());}
static void sky(Scene& s){s.settings.skyColor={.35f,.5f,.8f};s.settings.exposure=1.2f;s.settings.fog=.08f;}
static void gradient5(Scene& s){auto& o=s.settings;o.customGradient=true;o.gradientStops=5;o.gradientScale=1.6f;o.gradientRepeat=false;
 o.gradientLow={.05f,.05f,.4f};o.gradientHigh={1,.9f,.2f};o.boundedGradient=true;}
static void gradientUnbounded(Scene& s){auto& o=s.settings;o.customGradient=true;o.gradientStops=3;o.boundedGradient=false;o.gradientScale=.3f;o.gradientRepeat=true;}
static void emission(Scene& s){s.settings.emission=.35f;s.settings.sunStrength=.5f;s.settings.exposure=1.1f;}
static void sunOff(Scene& s){s.settings.sunStrength=0;s.settings.shadow=16;s.settings.ao=2;}
static void lamps(Scene& s){auto& o=s.settings;
 o.pointLights[0].enabled=true;o.pointLights[0].position={1.2f,.8f,1.5f};o.pointLights[0].color={1,.7f,.4f};o.pointLights[0].intensity=14;o.pointLights[0].range=7;
 o.pointLights[1].enabled=true;o.pointLights[1].cameraRelative=false;o.pointLights[1].position={-1,1.5f,-2};o.pointLights[1].color={.3f,.5f,1};o.pointLights[1].intensity=10;o.pointLights[1].range=6;
 o.sunStrength=.4f;}
static void lampsShadow(Scene& s){lamps(s);s.settings.pointShadows=true;s.settings.shadow=48;}
static void dof(Scene& s){auto& o=s.settings;o.dof=true;o.aperture=.08f;o.focusDistance=3.5f;o.dofSamples=8;}
static void dofQuality(Scene& s){dof(s);s.settings.quality=true;s.settings.samples=2;s.settings.dofSamples=3;}
static void gi4(Scene& s){auto& o=s.settings;o.giSamples=4;o.giSteps=24;o.giStrength=.8f;o.giRange=2.5f;o.skyColor={.2f,.3f,.5f};}
static void gi12(Scene& s){auto& o=s.settings;o.giSamples=12;o.giSteps=32;o.giStrength=.9f;o.giRange=3;o.skyColor={.25f,.3f,.45f};o.emission=.1f;}
static void giLamps(Scene& s){gi12(s);lampsShadow(s);}
static void giDof(Scene& s){gi4(s);dof(s);s.settings.dofSamples=6;}
static void progressive(Scene& s){gi4(s);s.settings.progressiveLighting=true;s.settings.lightingPasses=4;}
static void bloom(Scene& s){s.settings.bloom=.9f;s.settings.exposure=1.6f;}
static void adaptive(Scene& s){s.settings.adaptivePrecision=true;s.settings.minEpsilon=1e-6f;s.settings.pixelTolerance=.25f;}
static void relax(Scene& s){s.settings.relaxation=1.6f;}
static void stillBlock2(Scene& s){s.settings.stillBlock=2;}
static void everything(Scene& s){gi4(s);lampsShadow(s);gradient5(s);s.settings.bloom=.4f;s.settings.ao=4;s.settings.emission=.1f;adaptive(s);}

// Same algorithm as Renderer::applyBloom (private there), for the reference image.
static void cpuBloom(std::vector<Color>& px,float strength){
 std::vector<Vec> horizontal(size_t(W)*H);
 auto bright=[&](Color c){Vec v{c.r/255.f,c.g/255.f,c.b/255.f};float peak=std::fmax(v.x,std::fmax(v.y,v.z));return v*(peak>.6f?(peak-.6f)/peak:0.f);};
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){Vec sum;for(int dx=-6;dx<=6;++dx)sum=sum+bright(px[size_t(y)*W+std::max(0,std::min(W-1,x+dx))]);horizontal[size_t(y)*W+x]=sum*(1.f/13);}
 std::vector<Color> out(px.size());
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){Vec sum;for(int dy=-6;dy<=6;++dy)sum=sum+horizontal[size_t(std::max(0,std::min(H-1,y+dy)))*W+x];
  Color c=px[size_t(y)*W+x];Vec v=Vec{c.r/255.f,c.g/255.f,c.b/255.f}+sum*(strength/13);
  out[size_t(y)*W+x]={uint8_t(clamp(v.x,0,1)*255),uint8_t(clamp(v.y,0,1)*255),uint8_t(clamp(v.z,0,1)*255)};}
 px=out;
}

int main(int argc,char** argv){
 const char* filter=argc>1?argv[1]:"";const char* dump=argc>2?argv[2]:nullptr;
 if(SDL_Init(SDL_INIT_VIDEO)!=0){std::printf("SDL init: %s\n",SDL_GetError());return 1;}
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 SDL_Window* win=SDL_CreateWindow("t",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
 SDL_GLContext ctx=win?SDL_GL_CreateContext(win):nullptr;
 if(!ctx){std::printf("GL context: %s\n",SDL_GetError());return 1;}
 if(!loadGL()){std::printf("loadGL failed\n");return 1;}
 W=160;H=96;
 GpuRenderer gpu;std::string err;
 if(!gpu.init(err)||!gpu.resize(W,H)){std::printf("init: %s\n",err.c_str());return 1;}
 std::printf("renderer: %s\n",gpu.rendererName());
 Case cases[]={
  {"0 bulb8",0,none},{"1 bulb2",1,none},{"5 julia bulb",5,none},{"6 mandelbox",6,none},{"7 neg box",7,none},
  {"10 menger",10,none},{"11 twisted menger",11,none},{"12 tetra",12,none},{"13 kaleido tetra",13,none},{"14 box-bulb",14,none},
  {"16 expression",16,none},{"20 abs bulb",20,none},{"21 rot neg box",21,none},{"22 twisted julia",22,none},
  {"6 mandelbox quality",6,quality},{"0 bulb8 quality",0,quality},{"16 expr quality",16,quality},{"10 menger repeat",10,repeatOn},
  {"0 bulb8 ao+shadow",0,aoShadow},{"18 bulb4 algebraic",18,algebraic},{"6 box ao+shadow",6,aoShadow},{"23 menger wide",23,none},{"16 expr many ops",16,exprOps},
  // 3DS-era presets: repeat/corridor/adaptive precision (24..35), Kleinian (36..39)
  {"24 ruby chambers",24,none},{"25 blue vault",25,none},{"26 gold corridor",26,none},{"27 menger colonnade",27,none},
  {"28 twisted hall",28,none},{"29 bubble hall",29,none},{"30 tetra gallery",30,none},{"31 julia arcade",31,none},
  {"32 bulb garden",32,none},{"33 abs bulb 5",33,none},{"34 box-bulb 4",34,none},{"35 neg box deep",35,none},
  {"36 kleinian chambers",36,none},{"37 kleinian corridor",37,none},{"38 kleinian drops",38,none},{"39 kleinian gallery",39,none},
  {"36 kleinian quality",36,quality},{"38 kleinian ao+shadow",38,aoShadow},
  // lighting / colour features
  {"6 sky+fog",6,sky},{"0 gradient 5 stops",0,gradient5},{"20 gradient unbounded",20,gradientUnbounded},{"6 emission+sun.5",6,emission},
  {"6 sun off + ao",6,sunOff},{"6 point lights",6,lamps},{"37 point lights+shadow",37,lampsShadow},{"26 lamps+shadow",26,lampsShadow},
  {"0 adaptive precision",0,adaptive},{"6 relaxation 1.6",6,relax},{"24 ao32 shadow512",24,aoShadowHeavy},
  {"6 quality 6 samples (R2)",6,quality6},{"0 stillBlock 2",0,stillBlock2},
  // stochastic: depth of field, GI
  {"6 dof",6,dof},{"37 dof",37,dof},{"6 dof quality",6,dofQuality},{"6 gi4",6,gi4},{"6 gi12",6,gi12},{"36 gi12",36,gi12},
  {"38 gi+lamps",38,giLamps},{"37 gi+dof",37,giDof},{"6 progressive gi",6,progressive},
  {"0 bloom",0,bloom},{"36 everything",36,everything}};
 int ci=0;double worst=0;
 std::printf("%-26s %9s %9s %9s %8s %s\n","case","meanAbs","meanBias","big>60","gpu ms","note");
 for(const Case& c:cases){
  ++ci;
  if(*filter&&!std::strstr(c.name,filter))continue;
  Scene s;s.formula=preset(c.preset);s.camera=presetCamera(c.preset);s.settings=presetSettings(c.preset);c.tweak(s);
  std::string e;s.formula.validate(e);
  std::string why;bool sup=GpuRenderer::supports(s,why);
  if(!sup){std::printf("%-26s unsupported: %s\n",c.name,why.c_str());continue;}
  if(!gpu.begin(s,0,err)){std::printf("%-26s begin failed: %s\n",c.name,err.c_str());continue;}
  auto t0=std::chrono::steady_clock::now();
  int guard=0;while(!gpu.render(s,W,H,1000)&&++guard<100000);
  double gms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
  std::vector<Color> g;int gw,gh;gpu.readback(0,g,gw,gh);
  Rays rays(s);std::vector<Color> cpu(size_t(W)*H);
  unsigned nt=std::max(1u,std::thread::hardware_concurrency());std::vector<std::thread> pool;
  for(unsigned t=0;t<nt;++t)pool.emplace_back([&,t]{
   for(int y=int(t);y<H;y+=int(nt))for(int x=0;x<W;++x){RenderJob j;j.x=x;j.y=y;j.block=1;
    j.accumulate=s.settings.progressiveLighting&&((s.settings.giSamples>0&&s.settings.giStrength>0)||(s.settings.dof&&s.settings.aperture>0));
    if(j.accumulate){ // progressive passes: average the per-pass radiance like Renderer::step()
     Vec sum;int passes=std::max(1,s.settings.lightingPasses);
     for(int p=0;p<passes;++p){j.sample=uint32_t(p);sum=sum+renderJob(s,rays,j).radiance;}
     sum=sum*(1.f/passes);cpu[size_t(y)*W+x]={uint8_t(clamp(sum.x,0,1)*255),uint8_t(clamp(sum.y,0,1)*255),uint8_t(clamp(sum.z,0,1)*255)};
    }else{
     if(s.settings.stillBlock>1&&!s.settings.quality){j.block=s.settings.stillBlock;j.x=x/j.block*j.block;j.y=y/j.block*j.block;}
     cpu[size_t(y)*W+x]=renderJob(s,rays,j).color;}
   }});
  for(auto& t:pool)t.join();
  if(s.settings.bloom>0)cpuBloom(cpu,s.settings.bloom);
  double sum=0,bias=0;long big=0;
  for(size_t i=0;i<cpu.size();++i){const Color &k=cpu[i],&q=g[i];
   int d=std::abs(k.r-q.r)+std::abs(k.g-q.g)+std::abs(k.b-q.b);sum+=d/3.0;bias+=((q.r+q.g+q.b)-(k.r+k.g+k.b))/3.0;if(d>60)++big;}
  worst=std::max(worst,sum/cpu.size());
  std::printf("%-26s %9.3f %9.3f %9ld %8.0f\n",c.name,sum/cpu.size(),bias/cpu.size(),big,gms);
  if(dump){char nm[256];std::snprintf(nm,sizeof nm,"%s_%d.ppm",dump,ci-1);FILE* f=std::fopen(nm,"wb");std::fprintf(f,"P6\n%d %d\n255\n",W*2,H);
   for(int y=0;y<H;++y){std::fwrite(&cpu[size_t(y)*W],3,W,f);std::fwrite(&g[size_t(y)*W],3,W,f);}std::fclose(f);}
 }
 std::printf("worst mean abs diff %.3f\n",worst);
 if(!*filter){ // stereo + reduced-scale + strip budget smoke test
  Scene s;s.formula=preset(0);s.camera=presetCamera(0);
  gpu.begin(s,.03f,err);int calls=1;while(!gpu.render(s,W/2,H/2,.5f)&&calls<100000)++calls;
  std::printf("stereo eyes=%d, tiny-budget render took %d calls\n",gpu.eyes(),calls);
  std::vector<Color> a,b;int w,h;gpu.readback(0,a,w,h);gpu.readback(1,b,w,h);
  long d=0;for(size_t i=0;i<a.size();++i)d+=std::abs(a[i].r-b[i].r);std::printf("L/R differ sum %ld (%dx%d)\n",d,w,h);
 }
 gpu.shutdown();return 0;
}
