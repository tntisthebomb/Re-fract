// OpenGL 3.3 core fragment-shader ray marcher mirroring windows/engine/{renderer,formula,expression}.cpp.
//
// Layout of this file:
//   1. GLSL generation   (structure -> source text; numeric parameters stay uniforms)
//   2. GL helpers        (program compile, render targets)
//   3. GpuRenderer       (begin / render in TDR-safe strips / readback)
//
// Image orientation: the CPU renders with y pointing DOWN. The GL texture is bottom-left origin, so the
// shader maps gl_FragCoord row r to CPU row (renderH-1-r) (sample y = renderH - fragY). Texel row 0 is
// the BOTTOM of the picture, hence texture() sampled with GL conventions (v=0 bottom) shows it upright.
// A reduced-scale render occupies the bottom-left [0,renderW)x[0,renderH) corner of the texture.
#include "gpu_renderer.hpp"
#include "gl_loader.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace rf {
namespace {

// ---------------------------------------------------------------- 1. GLSL generation

std::string lit(float v){ // GLSL float literal that round-trips
 char b[64];std::snprintf(b,sizeof b,"%.9g",double(v));std::string s=b;
 if(s.find_first_of(".eEn")==std::string::npos)s+=".0";
 return v<0?"("+s+")":s;
}

const char* kVertex=
"#version 330 core\n"
"void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";

// Emits the unrolled dual-number evaluation of one expression (mirrors Expression::evaluateGeneric).
// Result goes to o<j>v (value), o<j>z (d/dxyz), o<j>c (d/dcxyz). Domain failures execute FAIL.
void genExpression(std::string& o,const Expression& e,int j){
 const char* vars[6]={"z.x","z.y","z.z","c.x","c.y","c.z"};
 char b[512];
 auto emit=[&](const char* fmt,auto... args){std::snprintf(b,sizeof b,fmt,args...);o+=b;};
 o+=" float o"+std::to_string(j)+"v;vec3 o"+std::to_string(j)+"z,o"+std::to_string(j)+"c;\n {\n";
 o+="  float V[32];vec3 Z[32],C[32];\n";
 int n=0;
 for(int i=0;i<e.count;++i){const Instruction& ins=e.code[i];Op op=ins.op;
  if(op==Op::Constant){emit("  V[%d]=%s;Z[%d]=vec3(0.0);C[%d]=vec3(0.0);\n",n,lit(ins.value).c_str(),n,n,n);++n;continue;}
  if(op==Op::Variable){int v=ins.variable;
   emit("  V[%d]=%s;Z[%d]=vec3(%d.0,%d.0,%d.0);C[%d]=vec3(%d.0,%d.0,%d.0);\n",n,vars[v],n,v==0,v==1,v==2,n,v==3,v==4,v==5);++n;continue;}
  bool binary=op==Op::Add||op==Op::Sub||op==Op::Mul||op==Op::Div||op==Op::Pow||op==Op::Min||op==Op::Max;
  int a=n-(binary?2:1),bb=n-1; // after popping b, a is the top
  if(binary)--n;
  std::string& blk=o;size_t blockStart=o.size();
  o+="  {float av=V[A],bv="+std::string(binary?"V[B]":"0.0")+",da=0.0,db=0.0;\n";
  switch(op){
   case Op::Add:o+="   V[A]=av+bv;da=1.0;db=1.0;\n";break;
   case Op::Sub:o+="   V[A]=av-bv;da=1.0;db=-1.0;\n";break;
   case Op::Mul:o+="   V[A]=av*bv;da=bv;db=av;\n";break;
   case Op::Div:o+="   if(abs(bv)<1e-12){FAIL}\n   V[A]=av/bv;da=1.0/bv;db=-av/(bv*bv);\n";break;
   case Op::Pow:
    o+="   bool isVar=any(notEqual(Z[B],vec3(0.0)))||any(notEqual(C[B],vec3(0.0)));\n"
       "   if(av<=0.0&&(isVar||floor(bv)!=bv)){FAIL}\n"
       "   if(av==0.0&&bv<1.0){FAIL}\n"
       "   if(!isVar&&bv==2.0){V[A]=av*av;da=2.0*av;}\n"
       "   else if(!isVar&&bv==3.0){V[A]=av*av*av;da=3.0*av*av;}\n"
       "   else{V[A]=rpow(av,bv);da=bv==0.0?0.0:bv*rpow(av,bv-1.0);}\n"
       "   db=(isVar&&av>0.0)?V[A]*log(av):0.0;\n";break;
   case Op::Min:o+="   bool useA=av<=bv;V[A]=useA?av:bv;da=useA?1.0:0.0;db=useA?0.0:1.0;\n";break;
   case Op::Max:o+="   bool useA=av>=bv;V[A]=useA?av:bv;da=useA?1.0:0.0;db=useA?0.0:1.0;\n";break;
   case Op::Neg:o+="   V[A]=-av;da=-1.0;\n";break;
   case Op::Sin:o+="   V[A]=sin(av);da=cos(av);\n";break;
   case Op::Cos:o+="   V[A]=cos(av);da=-sin(av);\n";break;
   case Op::Abs:o+="   V[A]=abs(av);da=av<0.0?-1.0:1.0;\n";break;
   case Op::Sqrt:o+="   if(av<0.0){FAIL}\n   V[A]=sqrt(av);da=0.5/max(V[A],1e-12);\n";break;
   case Op::Log:o+="   if(av<=0.0){FAIL}\n   V[A]=log(av);da=1.0/av;\n";break;
   case Op::Exp:o+="   V[A]=exp(av);da=V[A];\n";break;
   default:o+="   FAIL\n";break;
  }
  o+="   if(!fin(V[A])){FAIL}\n";
  o+=binary?"   Z[A]=da*Z[A]+db*Z[B];C[A]=da*C[A]+db*C[B];\n":"   Z[A]=da*Z[A];C[A]=da*C[A];\n";
  o+="   if(!fin3(Z[A])||!fin3(C[A])){FAIL}}\n";
  for(size_t at;(at=blk.find("[A]",blockStart))!=std::string::npos;)blk.replace(at,3,"["+std::to_string(a)+"]");
  for(size_t at;(at=blk.find("[B]",blockStart))!=std::string::npos;)blk.replace(at,3,"["+std::to_string(bb)+"]");
 }
 std::string s=std::to_string(j);
 o+="  o"+s+"v=V[0];o"+s+"z=Z[0];o"+s+"c=C[0];\n }\n";
}

const char* kindComment(Kind k){return kindName(k);}

void genStage(std::string& o,const Formula& f,const Stage& s,int i){
 std::string I=std::to_string(i);
 o+="  // stage "+I+": "+kindComment(s.kind)+"\n  {\n";
 const std::string P0="uP0["+I+"]",P1="uP1["+I+"]",P2="uP2["+I+"]";
 switch(s.kind){
  case Kind::BoxFold:
   o+="   float limit=abs("+P0+".x);z=2.0*clamp(z,-limit,limit)-z;\n";break;
  case Kind::SphereFold:
   o+="   float r2=dot(z,z);float k=r2<"+P1+".x?"+P1+".z:(r2<"+P1+".y?"+P1+".y/max(r2,1e-12):1.0);z*=k;dr*=k;\n";break;
  case Kind::Bulb:
   o+="   float r=length(z);\n   if(r<1e-12){z=vec3(0.0);dr=max(dr,1e-12);}else{\n"
      "    float power=radialPower(r,"+P0+".x-1.0,int("+P2+".z));\n"
      "    dr*=power*"+P2+".y;float nr=power*r;\n";
   if(f.algebraicBulb)
    o+="    if(uQuality==0&&"+P2+".w>0.5){\n"
       "     float radial=sqrt(z.x*z.x+z.y*z.y),st=radial/r,ct=z.z/r;\n"
       "     float sp=radial>0.0?z.y/radial:0.0,cp=radial>0.0?z.x/radial:1.0;\n"
       "     for(int n=int("+P0+".x);n>1;n>>=1){float ns=2.0*st*ct,nc=ct*ct-st*st;st=ns;ct=nc;ns=2.0*sp*cp;nc=cp*cp-sp*sp;sp=ns;cp=nc;}\n"
       "     z=vec3(nr*st*cp,nr*st*sp,nr*ct);\n    }else\n";
   o+="    {\n"
      "     float theta=acos(clamp(z.z/r,-1.0,1.0))*"+P1+".w;\n"
      "     float phi=(z.x==0.0&&z.y==0.0)?0.0:atan(z.y,z.x)*"+P2+".x;\n"
      "     float sinTheta=sin(theta);z=vec3(nr*sinTheta*cos(phi),nr*sinTheta*sin(phi),nr*cos(theta));\n    }\n   }\n";break;
  case Kind::Scale:
   o+="   z=z*"+P0+".x+c*"+P0+".y;dr=dr*abs("+P0+".x)"+(f.julia?";\n":"+abs("+P0+".y);\n");break;
  case Kind::Rotate:
   o+="   z=uRot["+I+"]*z;\n";break;
  case Kind::Offset:
   o+="   z+="+P0+".xyz;\n";break;
  case Kind::Absolute:
   o+="   z=abs(z);\n";break;
  case Kind::Sort:
   o+="   sort3(z);\n";break;
  case Kind::Menger:
   o+="   z=abs(z);sort3(z);\n"
      "   z=z*"+P0+".x-vec3("+P0+".y,"+P0+".y,0.0)*("+P0+".x-1.0);\n"
      "   float cut="+P0+".z*("+P0+".x-1.0);if(z.z>cut*0.5)z.z-=cut;dr*=abs("+P0+".x);\n";break;
  case Kind::Tetra:
   o+="   if(z.x+z.y<0.0){float t=z.x;z.x=-z.y;z.y=-t;}\n"
      "   if(z.x+z.z<0.0){float t=z.x;z.x=-z.z;z.z=-t;}\n"
      "   if(z.y+z.z<0.0){float t=z.y;z.y=-z.z;z.z=-t;}\n";break;
  case Kind::Expression:
   for(int j=0;j<3;++j)genExpression(o,s.expr[j],j);
   o+="   // Frobenius norms upper-bound the local Jacobian spectral norms.\n"
      "   float jz=dot(o0z,o0z)+dot(o1z,o1z)+dot(o2z,o2z),jc=dot(o0c,o0c)+dot(o1c,o1c)+dot(o2c,o2c);\n"
      "   dr=sqrt(jz)*dr"+(f.julia?std::string(";"):std::string("+sqrt(jc);"))+"\n   z=vec3(o0v,o1v,o2v);\n";break;
  default:o+="   FAIL\n";break;
 }
 o+="  }\n  if(!(fin3(z)&&fin(dr))||dr>1e30){FAIL}\n  trap=min(trap,dot(z,z));\n";
}

std::string generateFragment(const Formula& f){
 int n=std::max(1,f.activeCount);
 std::string N=std::to_string(n),o;
 o+="#version 330 core\n";
 o+="layout(location=0) out vec4 outColor;\n";
 // ---- uniforms
 o+="uniform vec2 uRes;uniform vec3 uOrigin,uForward,uRight,uUp,uLight;\n"
   "uniform float uTangent,uConv,uEye,uFar,uEps,uSafety,uFog,uExposure,uSpecular;\n"
   "uniform int uSteps,uQuality,uSamples,uAo,uShadow,uIter;\n"
   "uniform vec3 uGradLo,uGradHi;uniform float uGradScale,uGradOffset;uniform int uGradRepeat;\n"
   "uniform float uBail2,uTermR,uDScale;uniform vec3 uJulia,uPeriod;\n";
 o+="uniform vec4 uP0["+N+"],uP1["+N+"],uP2["+N+"];uniform mat3 uRot["+N+"];\n";
 // ---- helpers
 o+=R"(
bool fin(float x){return abs(x)<=3.4028234e38;}
bool fin3(vec3 v){return fin(v.x)&&fin(v.y)&&fin(v.z);}
float rpow(float a,float b){ // C pow() for integer b with negative a
 if(a<0.0){float m=pow(-a,b);return mod(b,2.0)==1.0?-m:m;}
 if(a==0.0)return b==0.0?1.0:0.0;
 return pow(a,b);
}
float radialPower(float r,float p,int n){
 if(n>=0){float o=1.0;for(int k=0;k<8;++k){if(n==0)break;if((n&1)!=0)o*=r;r*=r;n>>=1;}return o;}
 return pow(r,p);
}
void sort3(inout vec3 z){ // descending
 if(z.x<z.y)z.xy=z.yx;
 if(z.x<z.z)z.xz=z.zx;
 if(z.y<z.z)z.yz=z.zy;
}
#define FAIL {tr=trap;d=0.0;return false;}
)";
 // ---- repeat
 if(f.repeat)
  o+=R"(
vec3 repeatPoint(vec3 p){
 vec3 q=p;
 if(uPeriod.x>0.0)q.x=p.x-uPeriod.x*floor(p.x/uPeriod.x+0.5);
 if(uPeriod.y>0.0)q.y=p.y-uPeriod.y*floor(p.y/uPeriod.y+0.5);
 if(uPeriod.z>0.0)q.z=p.z-uPeriod.z*floor(p.z/uPeriod.z+0.5);
 return q;
}
float repeatBoundaryStep(vec3 point,vec3 dir){
 vec3 local=repeatPoint(point);float step=1e30;
 for(int k=0;k<3;++k){float per=uPeriod[k],pp=local[k],dd=dir[k];
  if(per<=0.0||abs(dd)<1e-12)continue;
  float boundary=dd>0.0?per*0.5:-per*0.5;step=min(step,max(0.0,(boundary-pp)/dd));}
 return step;
}
)";
 // ---- distance estimator
 o+="\n// Mirrors evaluateGenericDistance (trap always tracked). Returns false for an invalid sample.\n";
 o+="bool de(vec3 p,out float d,out float tr){\n";
 if(f.repeat)o+=" p=repeatPoint(p);\n";
 o+=" vec3 z=p,c="+std::string(f.julia?"uJulia":"p")+";float dr=1.0,trap=1e6;tr=trap;d=0.0;\n"
    " for(int it=0;it<uIter;++it){\n  if(dot(z,z)>uBail2)break;\n";
 for(int i=0;i<f.activeCount;++i)genStage(o,f,f.stages[f.active[i]],i);
 o+=" }\n float r=length(z);\n";
 o+=std::string(" d=")+(f.logarithmic?"0.5*log(max(r,1e-12))*r/max(dr,1e-12);\n":"r/max(dr,1e-12);\n");
 if(f.terminal==1)
  o+=" {vec3 q=abs(z)-vec3(uTermR);vec3 outside=max(q,0.0);d=(length(outside)+min(max(q.x,max(q.y,q.z)),0.0))/max(dr,1e-12);}\n";
 else if(f.terminal==2)
  o+=" d=(max(max(-z.x-z.y-z.z,-z.x+z.y+z.z),max(z.x-z.y+z.z,z.x+z.y-z.z))-uTermR)*0.57735027/max(dr,1e-12);\n";
 o+=" d=max(0.0,d)/uDScale;tr=sqrt(trap);\n return fin(d);\n}\n";
 // ---- ray marcher
 o+=R"(
vec3 gradientColor(float trap){
 float t=trap*uGradScale+uGradOffset;
 t=uGradRepeat!=0?t-floor(t):clamp(t,0.0,1.0);
 return uGradLo*(1.0-t)+uGradHi*t;
}
float dist(vec3 p){float d,tr;de(p,d,tr);return d;}
vec3 unit(vec3 v){float n=length(v);return n>1e-12?v/n:vec3(0.0,0.0,1.0);}
ivec3 quantize(vec3 v){return ivec3(floor(clamp(v,0.0,1.0)*255.0));}
const vec3 BG=vec3(0.025,0.028,0.024);
const ivec3 MAGENTA=ivec3(180,20,90);

// x,y: CPU pixel coordinates (y down, in [0,uRes]).
ivec3 sampleRay(float x,float y){
 float aspect=uRes.x/uRes.y;
 float px=(2.0*x/uRes.x-1.0)*uTangent*aspect-uEye/uConv;
 float py=(1.0-2.0*y/uRes.y)*uTangent;
 vec3 dir=unit(uForward+uRight*px+uUp*py);
 vec3 origin=uOrigin;
 float t=0.0,trap=0.0,d,tr;bool hit=false;
 float epsScale=uQuality!=0?0.5:1.0;
 for(int i=0;i<uSteps;++i){
  if(!(t<uFar))break;
  vec3 pt=origin+dir*t;
  if(!de(pt,d,tr))return MAGENTA;
  float eps=uEps*epsScale*max(1.0,t*0.2);
  if(d<eps){hit=true;trap=tr;break;}
  float advance=max(d*uSafety,eps*0.25);
)";
 if(f.repeat)o+="  advance=min(advance,repeatBoundaryStep(pt,dir)+eps*0.25);\n";
 o+=R"(  t+=advance;
 }
 if(!hit)return quantize(BG+vec3(0.018,0.012,0.006)*(1.0-y/uRes.y));
 vec3 p=origin+dir*t;
 de(p,d,trap);
 float e=max(uEps,t*uEps*0.25);
 // Tetrahedral normal
 vec3 v1=vec3(1.0,-1.0,-1.0),v2=vec3(-1.0,-1.0,1.0),v3=vec3(-1.0,1.0,-1.0),v4=vec3(1.0,1.0,1.0);
 vec3 n=unit(v1*dist(p+v1*e)+v2*dist(p+v2*e)+v3*dist(p+v3*e)+v4*dist(p+v4*e));
 float diffuse=max(0.0,dot(n,uLight)),ao=1.0,shadow=1.0;
 float baseSpec=uSpecular>0.0?pow(max(0.0,dot(n,unit(uLight-dir))),24.0)*uSpecular:0.0;
 for(int i=1;i<=uAo&&ao>0.15;++i){float h=e*6.0*float(i);ao-=max(0.0,h-dist(p+n*h))/h*(0.35/float(i));}
 if(uShadow!=0&&(diffuse>0.0||baseSpec>0.0)){
  float st=e*8.0;vec3 start=p+n*(e*4.0);
  for(int i=0;i<uShadow&&st<8.0;++i){
   float sd,str;bool ok=de(start+uLight*st,sd,str);
   if(!ok||sd<e){shadow=0.15;break;}
   shadow=min(shadow,12.0*sd/st);st+=max(sd*uSafety,e);
  }
 }
 float spec=baseSpec*shadow;
 float lighting=clamp(ao,0.15,1.0)*(0.22+0.78*diffuse*clamp(shadow,0.15,1.0));
 vec3 color=gradientColor(trap)*lighting+vec3(spec);
 float fog=exp(-t*uFog);
 color=(color*fog+BG*(1.0-fog))*uExposure;
 return quantize(color);
}

void main(){
 // GL row r (bottom-up) -> CPU row uRes.y-1-r (top-down); see file header.
 float xl=floor(gl_FragCoord.x),rowTop=uRes.y-1.0-floor(gl_FragCoord.y);
 int count=uQuality!=0?max(uSamples,1):1;
 ivec3 sum=ivec3(0);
 for(int i=0;i<count;++i){
  float jx=count==1?0.5:((i&1)!=0?0.75:0.25),jy=count<=2?0.5:((i&2)!=0?0.75:0.25);
  sum+=sampleRay(xl+jx,rowTop+jy);
 }
 outColor=vec4(vec3(sum/count)/255.0,1.0);
}
)";
 return o;
}

// ---------------------------------------------------------------- 2. GL helpers

struct Program {
 GLuint id=0;
 std::unordered_map<std::string,GLint> locs;
 GLint loc(const char* name){auto it=locs.find(name);if(it!=locs.end())return it->second;
  GLint l=glGetUniformLocation(id,name);locs[name]=l;return l;}
};

GLuint compile(GLenum type,const std::string& src,std::string& log){
 GLuint s=glCreateShader(type);const char* p=src.c_str();glShaderSource(s,1,&p,nullptr);glCompileShader(s);
 GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
 if(!ok){GLint len=0;glGetShaderiv(s,GL_INFO_LOG_LENGTH,&len);std::string l(size_t(std::max(len,1)),'\0');
  glGetShaderInfoLog(s,len,nullptr,&l[0]);log=l.c_str();glDeleteShader(s);return 0;}
 return s;
}

bool build(Program& prog,const std::string& frag,std::string& error){
 std::string log;
 GLuint vs=compile(GL_VERTEX_SHADER,kVertex,log);
 if(!vs){error="vertex shader: "+log;return false;}
 GLuint fs=compile(GL_FRAGMENT_SHADER,frag,log);
 if(!fs){glDeleteShader(vs);error="fragment shader: "+log;return false;}
 GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);
 glDeleteShader(vs);glDeleteShader(fs);
 GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
 if(!ok){GLint len=0;glGetProgramiv(p,GL_INFO_LOG_LENGTH,&len);std::string l(size_t(std::max(len,1)),'\0');
  glGetProgramInfoLog(p,len,nullptr,&l[0]);error="link: "+std::string(l.c_str());glDeleteProgram(p);return false;}
 prog.id=p;prog.locs.clear();return true;
}

using Clock=std::chrono::steady_clock;
}

// ---------------------------------------------------------------- 3. GpuRenderer

struct GpuRenderer::Impl {
 std::map<std::string,Program> programs; // keyed by generated fragment source
 Program* current=nullptr;
 GLuint vao=0;
 GLuint tex[2]={0,0},fbo[2]={0,0};
 int width=0,height=0;
 int eyeCount=1;float eyeOffset=0;
 bool active=false;
 int renderW=0,renderH=0;      // region of the image being / last rendered
 int rowsDone[2]={0,0};
 double msPerRow=0;int lastRows=2;
 std::string renderer;
 bool inited=false;

 void destroyTargets(){
  for(int e=0;e<2;++e){
   if(fbo[e]){glDeleteFramebuffers(1,&fbo[e]);fbo[e]=0;}
   if(tex[e]){glDeleteTextures(1,&tex[e]);tex[e]=0;}
  }
 }
 void setUniforms(const Scene& scene,int rw,int rh);
};

GpuRenderer::GpuRenderer(){impl=new Impl;}
GpuRenderer::~GpuRenderer(){delete impl;}

bool GpuRenderer::init(std::string& error){
 if(impl->inited)return true;
 const char* r=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
 impl->renderer=r?r:"unknown";
 glGenVertexArrays(1,&impl->vao);
 if(!impl->vao){error="could not create vertex array";return false;}
 impl->inited=true;return true;
}

void GpuRenderer::shutdown(){
 if(!impl->inited)return;
 impl->destroyTargets();
 for(auto& kv:impl->programs)if(kv.second.id)glDeleteProgram(kv.second.id);
 impl->programs.clear();impl->current=nullptr;
 if(impl->vao)glDeleteVertexArrays(1,&impl->vao);
 impl->vao=0;impl->width=impl->height=0;impl->active=false;impl->inited=false;
}

bool GpuRenderer::resize(int w,int h){
 if(w<1||h<1)return false;
 if(w==impl->width&&h==impl->height&&impl->tex[0])return true;
 impl->destroyTargets();
 for(int e=0;e<2;++e){
  glGenTextures(1,&impl->tex[e]);glBindTexture(GL_TEXTURE_2D,impl->tex[e]);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1,&impl->fbo[e]);glBindFramebuffer(GL_FRAMEBUFFER,impl->fbo[e]);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,impl->tex[e],0);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  if(!ok){glBindFramebuffer(GL_FRAMEBUFFER,0);impl->destroyTargets();impl->width=impl->height=0;return false;}
 }
 glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
 impl->width=w;impl->height=h;impl->active=false;
 return true;
}

bool GpuRenderer::begin(const Scene& scene,float eyeOffset,std::string& error){
 Impl& m=*impl;m.active=false;
 const Formula& f=scene.formula;
 if(f.activeCount<1||f.activeCount>MaxStages){error="formula has no active stages (call Formula::validate)";return false;}
 std::string src=generateFragment(f);
 auto it=m.programs.find(src);
 if(it==m.programs.end()){
  Program prog;if(!build(prog,src,error))return false;
  // Keep the cache small; never drop the program currently in use.
  while(m.programs.size()>=6){auto victim=m.programs.begin();if(&victim->second==m.current)++victim;
   if(victim==m.programs.end())break;glDeleteProgram(victim->second.id);m.programs.erase(victim);}
  it=m.programs.emplace(src,std::move(prog)).first;
  m.msPerRow=0;m.lastRows=2; // new shader: re-measure cost
 }
 m.current=&it->second;
 m.eyeOffset=eyeOffset;m.eyeCount=eyeOffset==0?1:2;
 m.rowsDone[0]=m.rowsDone[1]=0;
 m.renderW=m.renderH=0;
 m.active=m.width>0;
 if(!m.active){error="resize() has not been called";return false;}
 return true;
}

void GpuRenderer::Impl::setUniforms(const Scene& scene,int rw,int rh){
 Program& p=*current;const Formula& f=scene.formula;const Settings& o=scene.settings;
 Rays rays(scene);
 auto u1f=[&](const char* n,float v){glUniform1f(p.loc(n),v);};
 auto u1i=[&](const char* n,int v){glUniform1i(p.loc(n),v);};
 auto u3f=[&](const char* n,Vec v){glUniform3f(p.loc(n),v.x,v.y,v.z);};
 glUniform2f(p.loc("uRes"),float(rw),float(rh));
 u3f("uForward",rays.forward);u3f("uRight",rays.right);u3f("uUp",rays.up);u3f("uLight",rays.light);
 u1f("uTangent",rays.tangent);u1f("uConv",o.convergence);
 u1f("uFar",o.farClip);u1f("uEps",o.epsilon);u1f("uSafety",o.safety);u1f("uFog",o.fog);
 u1f("uExposure",o.exposure);u1f("uSpecular",o.specular);
 u1i("uQuality",o.quality?1:0);u1i("uSteps",o.quality?std::min(512,o.steps*2):o.steps);
 u1i("uSamples",std::max(1,std::min(o.samples,8)));u1i("uAo",std::max(0,o.ao));u1i("uShadow",std::max(0,o.shadow));
 // palette: same selection as gradientColor()
 Vec lo=o.gradientLow,hi=o.gradientHigh;
 if(!o.customGradient){int n=int(o.palette)%4;
  if(n==1){lo={.13f,.4f,.48f};hi={.72f,.85f,.55f};}
  else if(n==2){lo={.3f,.09f,.36f};hi={.95f,.43f,.63f};}
  else if(n==3){lo={.18f,.19f,.17f};hi={.85f,.86f,.73f};}
  else{lo={.18f,.06f,.025f};hi={.95f,.58f,.22f};}
 }
 u3f("uGradLo",lo);u3f("uGradHi",hi);u1f("uGradScale",o.gradientScale);u1f("uGradOffset",o.gradientOffset);
 u1i("uGradRepeat",o.gradientRepeat?1:0);
 u1i("uIter",f.iterations);u1f("uBail2",f.bailout*f.bailout);u1f("uTermR",f.terminalRadius);u1f("uDScale",f.derivativeScale);
 u3f("uJulia",f.constant);u3f("uPeriod",f.repeatPeriod);
 // per-stage parameters, indexed by position in the active-stage list
 int n=f.activeCount;
 std::vector<float> p0(size_t(n)*4),p1(size_t(n)*4),p2(size_t(n)*4),rot(size_t(n)*9);
 for(int i=0;i<n;++i){const Stage& s=f.stages[f.active[i]];
  bool alg=(s.a==2||s.a==4||s.a==8||s.a==16)&&s.b==1&&s.c==1;
  float a0[4]={s.a,s.b,s.c,0},a1[4]={s.minimum2,s.fixed2,s.innerScale,s.thetaPower},a2[4]={s.phiPower,s.stretch,float(s.integerPower),alg?1.f:0.f};
  std::memcpy(&p0[size_t(i)*4],a0,sizeof a0);std::memcpy(&p1[size_t(i)*4],a1,sizeof a1);std::memcpy(&p2[size_t(i)*4],a2,sizeof a2);
  std::memcpy(&rot[size_t(i)*9],s.rotation.data(),9*sizeof(float));
 }
 glUniform4fv(p.loc("uP0"),n,p0.data());glUniform4fv(p.loc("uP1"),n,p1.data());glUniform4fv(p.loc("uP2"),n,p2.data());
 glUniformMatrix3fv(p.loc("uRot"),n,GL_TRUE,rot.data()); // rotation is row-major
}

bool GpuRenderer::render(const Scene& scene,int renderW,int renderH,float budgetMs){
 Impl& m=*impl;
 if(!m.active||!m.current)return true;
 renderW=std::max(1,std::min(renderW,m.width));renderH=std::max(1,std::min(renderH,m.height));
 if(m.renderW!=renderW||m.renderH!=renderH){ // region changed mid-image: restart it
  m.renderW=renderW;m.renderH=renderH;m.rowsDone[0]=m.rowsDone[1]=0;}
 auto t0=Clock::now();
 glUseProgram(m.current->id);glBindVertexArray(m.vao);
 glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_SCISSOR_TEST);
 glViewport(0,0,m.width,m.height);
 m.setUniforms(scene,renderW,renderH);
 Rays rays(scene);
 const double target=std::max(2.0,std::min(25.0,double(budgetMs)));
 bool complete=true;
 for(int e=0;e<m.eyeCount;++e){
  glBindFramebuffer(GL_FRAMEBUFFER,m.fbo[e]);
  float eye=m.eyeCount==1?0.f:(e?m.eyeOffset:-m.eyeOffset);
  Vec origin=scene.camera.position+rays.right*eye;
  glUniform1f(m.current->loc("uEye"),eye);
  glUniform3f(m.current->loc("uOrigin"),origin.x,origin.y,origin.z);
  while(m.rowsDone[e]<renderH){
   double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
   if(elapsed>=budgetMs){complete=false;break;}
   int rows=m.msPerRow>0?int(target/m.msPerRow):m.lastRows;
   rows=std::max(1,std::min({rows,m.lastRows*2,renderH-m.rowsDone[e]}));
   glScissor(0,m.rowsDone[e],renderW,rows);
   auto s0=Clock::now();
   glDrawArrays(GL_TRIANGLES,0,3);glFinish();
   double ms=std::chrono::duration<double,std::milli>(Clock::now()-s0).count();
   double per=ms/rows;
   m.msPerRow=m.msPerRow>0?std::max(per,m.msPerRow*0.8):per; // react fast to slowdowns, relax slowly
   m.lastRows=rows;m.rowsDone[e]+=rows;
  }
  if(m.rowsDone[e]<renderH)complete=false;
  if(!complete)break;
 }
 glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glUseProgram(0);
 return complete;
}

int GpuRenderer::eyes() const{return impl->eyeCount;}
unsigned GpuRenderer::texture(int eye) const{return impl->tex[impl->eyeCount==1?0:std::max(0,std::min(1,eye))];}
const char* GpuRenderer::rendererName() const{return impl->renderer.c_str();}

bool GpuRenderer::readback(int eye,std::vector<Color>& out,int& width,int& height){
 Impl& m=*impl;
 int e=m.eyeCount==1?0:std::max(0,std::min(1,eye));
 int w=m.renderW?m.renderW:m.width,h=m.renderH?m.renderH:m.height;
 if(!m.fbo[e]||w<1||h<1)return false;
 std::vector<uint8_t> raw(size_t(w)*h*4);
 glFinish();glBindFramebuffer(GL_FRAMEBUFFER,m.fbo[e]);glPixelStorei(GL_PACK_ALIGNMENT,1);
 glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
 glBindFramebuffer(GL_FRAMEBUFFER,0);
 out.resize(size_t(w)*h);
 for(int y=0;y<h;++y){const uint8_t* src=&raw[size_t(h-1-y)*w*4];
  for(int x=0;x<w;++x)out[size_t(y)*w+x]={src[x*4],src[x*4+1],src[x*4+2]};}
 width=w;height=h;return true;
}
}
