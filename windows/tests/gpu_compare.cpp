// GPU vs CPU comparison for the OpenGL ray marcher.
// Build (Linux, from windows/):
//   g++ -std=c++17 -O2 -Iengine -Isrc $(pkg-config --cflags sdl2) tests/gpu_compare.cpp src/gpu_renderer.cpp \
//       src/gl_loader.cpp engine/renderer.cpp engine/formula.cpp engine/expression.cpp engine/surface.cpp \
//       $(pkg-config --libs sdl2) -lGL -o /tmp/gpu_compare
// Run: LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a /tmp/gpu_compare [dump.ppm]
#include "gpu_renderer.hpp"
#include "gl_loader.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace rf;

struct Case {const char* name;int preset;void (*tweak)(Scene&);};
static void none(Scene&){}
static void repeatOn(Scene& s){s.formula.repeat=true;s.formula.repeatPeriod={6,6,6};s.camera.position={0,0,-2.5f};}
static void quality(Scene& s){s.settings.quality=true;s.settings.samples=4;}
static void aoShadow(Scene& s){s.settings.ao=3;s.settings.shadow=24;s.settings.specular=.4f;}
static void algebraic(Scene& s){s.formula.algebraicBulb=true;}
static void exprOps(Scene& s){auto& st=s.formula.stages[0];
 st.text={{"x*x-y*y-z*z+cx+0.05*sin(3*y)","2*x*y+cy+0.1*abs(z)*min(x,0.3)","2*x*z+cz-0.05*sqrt(x*x+1)+0.02*pow(y,2)/(1+exp(-z))+0.01*log(x*x+2)*cos(x)"}};
 std::string e;s.formula.validate(e);if(!e.empty())std::printf("expr err %s\n",e.c_str());}

int main(int argc,char** argv){
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
 Case cases[]={{"0 bulb8",0,none},{"1 bulb2",1,none},{"5 julia bulb",5,none},{"6 mandelbox",6,none},{"7 neg box",7,none},
  {"10 menger",10,none},{"11 twisted menger",11,none},{"12 tetra",12,none},{"13 kaleido tetra",13,none},{"14 box-bulb",14,none},
  {"16 expression",16,none},{"20 abs bulb",20,none},{"21 rot neg box",21,none},{"22 twisted julia",22,none},
  {"6 mandelbox quality",6,quality},{"0 bulb8 quality",0,quality},{"16 expr quality",16,quality},{"10 menger repeat",10,repeatOn},
  {"0 bulb8 ao+shadow",0,aoShadow},{"18 bulb4 algebraic",18,algebraic},{"6 box ao+shadow",6,aoShadow},{"23 menger wide",23,none},{"16 expr many ops",16,exprOps}};
 int ci=0;
 for(const Case& c:cases){
  Scene s;s.formula=preset(c.preset);s.camera=presetCamera(c.preset);c.tweak(s);
  if(c.tweak==aoShadow){}
  std::string e;s.formula.validate(e);
  if(!gpu.begin(s,0,err)){std::printf("%-20s begin failed: %s\n",c.name,err.c_str());continue;}
  int guard=0;while(!gpu.render(s,W,H,1000)&&++guard<100000);
  std::vector<Color> g;int gw,gh;gpu.readback(0,g,gw,gh);
  Rays rays(s);double sum=0;long big=0;std::vector<Color> cpu(size_t(W)*H);
  for(int y=0;y<H;++y)for(int x=0;x<W;++x){
   RenderJob j;j.x=x;j.y=y;j.block=1;Color k=renderJob(s,rays,j).color;cpu[size_t(y)*W+x]=k;const Color& q=g[size_t(y)*W+x];
   int d=std::abs(k.r-q.r)+std::abs(k.g-q.g)+std::abs(k.b-q.b);sum+=d/3.0;if(d>60)++big;}
  std::printf("%-20s mean abs diff %.3f  (pixels >60 total: %ld/%d)\n",c.name,sum/(W*H),big,W*H);
  if(argc>1&&(ci==22||ci==9||ci==10||ci==5)){char nm[256];std::snprintf(nm,sizeof nm,"%s_%d.ppm",argv[1],ci);FILE* f=std::fopen(nm,"wb");std::fprintf(f,"P6\n%d %d\n255\n",W*2,H);
   for(int y=0;y<H;++y){std::fwrite(&cpu[size_t(y)*W],3,W,f);std::fwrite(&g[size_t(y)*W],3,W,f);}std::fclose(f);}
  ++ci;
 }
 // stereo + reduced-scale + strip budget smoke test
 { Scene s;s.formula=preset(0);s.camera=presetCamera(0);
   gpu.begin(s,.03f,err);int calls=1;while(!gpu.render(s,W/2,H/2,.5f)&&calls<100000)++calls;
   std::printf("stereo eyes=%d, tiny-budget render took %d calls\n",gpu.eyes(),calls);
   std::vector<Color> a,b;int w,h;gpu.readback(0,a,w,h);gpu.readback(1,b,w,h);
   long d=0;for(size_t i=0;i<a.size();++i)d+=std::abs(a[i].r-b[i].r);std::printf("L/R differ sum %ld (%dx%d)\n",d,w,h);}
 gpu.shutdown();return 0;
}
