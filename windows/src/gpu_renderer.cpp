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
#include <climits>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

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
  case Kind::KleinianFold:
   o+="   vec3 ext=vec3("+P0+".x,"+P0+".y,"+P0+".z);z=2.0*clamp(z,-ext,ext)-z;\n";break;
  case Kind::Inversion:
   o+="   float k=max(1.0,"+P1+".x/max(dot(z,z),1e-12));z*=k;dr*=k;\n";break;
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

// Feature bits compiled into the shader so disabled features cost nothing per pixel (toggling one recompiles).
enum Feature:unsigned {FeatGI=1,FeatFog=2,FeatPoint=4,FeatLens=8};

std::string generateFragment(const Formula& f,unsigned feat){
 int n=std::max(1,f.activeCount);
 std::string N=std::to_string(n),o;
 o+="#version 330 core\n";
 o+="#define F_GI "+std::to_string((feat&FeatGI)?1:0)+"\n#define F_FOG "+std::to_string((feat&FeatFog)?1:0)+
    "\n#define F_POINT "+std::to_string((feat&FeatPoint)?1:0)+"\n#define F_LENS "+std::to_string((feat&FeatLens)?1:0)+"\n";
 o+="layout(location=0) out vec4 outColor;\n";
 // ---- uniforms
 o+="uniform vec2 uRes;uniform vec3 uOrigin,uForward,uRight,uUp,uLight;\n"
   "uniform float uTangent,uConv,uEye,uFar,uEps,uSafety,uFog,uExposure,uSpecular,uSun,uEmission,uRelax;\n"
   "uniform int uSteps,uQuality,uCount,uPass,uOutMode,uJitter,uAo,uShadow,uIter,uBlock,uLens,uVaried,uLinear,uAdaptive;\n"
   "uniform float uMinEps,uPixTol,uAperture,uFocus;\n"
   "uniform vec3 uSky;\n"
   "uniform int uFogOn,uFogSteps,uFogShadow;uniform float uFogG,uFogAmbient,uFogScatter;uniform vec3 uFogColor;\n"
   "uniform int uGiSamples,uGiSteps;uniform float uGiStrength,uGiRange;\n"
   "uniform int uPointMask,uPointShadows;uniform vec3 uPointPos[2],uPointCol[2];uniform float uPointInt[2],uPointRange[2];\n"
   "uniform vec3 uGradLo,uGradHi,uGradMid[3];uniform float uGradScale,uGradOffset;uniform int uGradRepeat,uGradStops,uGradBounded;\n"
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
float radical(uint n,uint base){ // Halton radical inverse, same float recurrence as the CPU
 float value=0.0,weight=1.0/float(base);
 while(n!=0u){value+=float(n%base)*weight;n/=base;weight/=float(base);}
 return value;
}
void pattern(int i,int count,out float jx,out float jy){ // rf::samplePattern
 if(count<=4){jx=count==1?0.5:((i&1)!=0?0.75:0.25);jy=count<=2?0.5:((i&2)!=0?0.75:0.25);}
 else{jx=fract(0.5+float(i)*0.75487767);jy=fract(0.5+float(i)*0.56984029);}
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
 else if(f.terminal==3)
  o+=" {float xy=sqrt(z.x*z.x+z.y*z.y);d=max(xy-uTermR,abs(xy*z.z)/max(r,1e-12))/max(dr,1e-12);}\n";
 o+=" d=max(0.0,d)/uDScale;tr=sqrt(trap);\n return fin(d);\n}\n";
 // ---- shading helpers
 o+=R"(
float dist(vec3 p){float d,tr;de(p,d,tr);return d;}
vec3 unit(vec3 v){float n=length(v);return n>1e-12?v/n:vec3(0.0,0.0,1.0);}
ivec3 quantize(vec3 v){return ivec3(clamp(v,0.0,1.0)*255.0);}
const vec3 TV[4]=vec3[4](vec3(1.0,-1.0,-1.0),vec3(-1.0,-1.0,1.0),vec3(-1.0,1.0,-1.0),vec3(1.0,1.0,1.0));
vec3 tetraNormal(vec3 p,float e){ // four DE evaluations, same summation order as the CPU
 vec3 s=vec3(0.0);
 for(int k=0;k<4;++k){vec3 v=TV[k];s+=v*dist(p+v*e);}
 return unit(s);
}
vec3 gradientColor(float trap){
 float inp=uGradBounded!=0?max(trap,0.0)/(1.0+max(trap,0.0)):trap;
 float t=inp*uGradScale+uGradOffset;
 t=uGradRepeat!=0?t-floor(t):clamp(t,0.0,1.0);
 if(uGradStops>2){
  int count=min(5,uGradStops);float u=t*float(count-1);int index=min(count-2,int(u));float mx=u-float(index);
  vec3 a=index==0?uGradLo:(index==count-1?uGradHi:uGradMid[index-1]);
  int j=index+1;
  vec3 b=j==0?uGradLo:(j==count-1?uGradHi:uGradMid[j-1]);
  return a*(1.0-mx)+b*mx;
 }
 return uGradLo*(1.0-t)+uGradHi*t;
}
float hitTol(float t){ // rf::hitTolerance
 float ceiling=uEps*(uQuality!=0?0.5:1.0)*max(1.0,t*0.2);
 if(uAdaptive==0)return ceiling;
 return min(ceiling,max(uMinEps,2.0*t*uTangent/uRes.y*uPixTol));
}
float epsAt(vec3 p,float t){ // hit/step tolerance at p, floored by the float resolution
 float e=uAdaptive!=0?hitTol(t):max(uEps,t*uEps*0.25);
 if(uAdaptive!=0)e=max(e,4.0*1.1920929e-7*max(1.0,max(abs(p.x),max(abs(p.y),abs(p.z)))));
 return e;
}
float fogJitter(uint px,uint py,uint pass){ // rf::fogJitter
 uint h=px*73856093u^py*19349663u^(pass*83492791u);
 h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
 return float(h>>8)*(1.0/16777216.0);
}
)";
 const std::string stepRepeat=f.repeat?"stride=min(stride,repeatBoundaryStep(q,DIR)+EPS*0.25);\n":"";
 auto withRepeat=[&](std::string text,const char* dir,const char* eps){
  std::string line=stepRepeat;
  for(size_t at;(at=line.find("DIR"))!=std::string::npos;)line.replace(at,3,dir);
  for(size_t at;(at=line.find("EPS"))!=std::string::npos;)line.replace(at,3,eps);
  for(size_t at;(at=text.find("@REPEAT@"))!=std::string::npos;)text.replace(at,8,line);
  return text;
 };
 // ---- point lights
 o+=withRepeat(R"(
#if F_POINT
void pointLighting(vec3 position,vec3 normal,float offset,vec3 view,bool highlights,inout vec3 illumination,inout vec3 gloss){
 for(int i=0;i<2;++i){
  if((uPointMask&(1<<i))==0)continue;
  vec3 delta=uPointPos[i]-position;float len=length(delta);
  if(len>=uPointRange[i]||len<=offset)continue;
  vec3 direction=delta*(1.0/len);float lambert=max(0.0,dot(normal,direction));
  float specular=highlights&&uSpecular>0.0?pow(max(0.0,dot(normal,unit(direction-view))),24.0)*uSpecular:0.0;
  if(lambert==0.0&&specular==0.0)continue;
  float visibility=1.0;
  if(uPointShadows!=0&&uShadow>0){
   float travel=offset*8.0;vec3 start=position+normal*(offset*4.0);
   for(int k=0;k<uShadow&&travel<len;++k){
    vec3 q=start+direction*travel;float sd,str;bool ok=de(q,sd,str);
    if(!ok||sd<offset){visibility=0.0;break;}
    visibility=min(visibility,12.0*sd/travel);
    float stride=max(sd*uSafety,offset);
    @REPEAT@
    travel+=stride;
   }
   if(travel<len)visibility=0.0;
  }
  float falloff=1.0-len/uPointRange[i];
  float power=uPointInt[i]*falloff*falloff/(1.0+len*len)*visibility;
  illumination+=uPointCol[i]*(lambert*power);gloss+=uPointCol[i]*(specular*power);
 }
}
#endif
)","direction","offset");
 // ---- indirect lighting (GI)
 o+=withRepeat(R"(
#if F_GI
const vec2 GIOFF[4]=vec2[4](vec2(0.35,0.35),vec2(-0.65,0.25),vec2(0.25,-0.65),vec2(-0.45,-0.45));
vec3 indirectLight(vec3 p,vec3 n,float e,float x,float y,uint seq){
 vec3 axis=abs(n.z)<0.9?vec3(0.0,0.0,1.0):vec3(0.0,1.0,0.0);
 vec3 u=unit(cross(axis,n)),v=cross(n,u),incoming=vec3(0.0);
 for(int j=0;j<uGiSamples;++j){
  float a=GIOFF[j&3].x,b=GIOFF[j&3].y;
  if(uVaried!=0||uGiSamples>4){
   uint index=seq*uint(uGiSamples)+uint(j)+1u;
   uint hash=uint(x)*1973u+uint(y)*9277u;
   float radius=sqrt(radical(index,2u)),angle=6.2831853*mod(radical(index,3u)+float(hash&65535u)/65536.0,1.0);
   a=radius*cos(angle);b=radius*sin(angle);
  }
  vec3 direction=u*a+v*b+n*sqrt(max(0.0,1.0-a*a-b*b));
  vec3 start=p+n*(e*4.0);float travel=e*4.0;bool bounced=false,valid=true;
  for(int k=0;k<uGiSteps&&travel<uGiRange;++k){
   vec3 q=start+direction*travel;float d,tr;
   if(!de(q,d,tr)){valid=false;break;}
   if(d<e){
    vec3 bn=tetraNormal(q,e);
    vec3 bounce=gradientColor(tr),local=vec3(0.0),unused=vec3(0.0);
#if F_POINT
    if(uPointMask!=0)pointLighting(q,bn,e,direction,false,local,unused);
#endif
    incoming+=bounce*(0.15+0.85*max(0.0,dot(bn,uLight))*uSun+uEmission)+bounce*local;
    bounced=true;break;
   }
   float stride=max(d*uSafety,e);
   @REPEAT@
   travel+=stride;
  }
  // Exhausted rays are treated as occluded, rather than leaking skylight.
  if(valid&&!bounced&&travel>=uGiRange)incoming+=uSky*(0.5+0.5*max(0.0,direction.y));
 }
 return incoming*(uGiStrength/float(uGiSamples));
}
#endif
)","direction","e");
 // ---- volumetric fog (mirrors the VOLUMETRIC FOG SPEC in engine/renderer.cpp)
 o+=withRepeat(R"(
#if F_FOG
float fogPhase(float c){float g=uFogG,g2=g*g;return (1.0-g2)/pow(max(1e-6,1.0+g2-2.0*g*c),1.5);}
float fogVis(vec3 P,vec3 L,float maxDist,float eps){
 if(uFogShadow<=0)return 1.0;
 float vis=1.0,travel=eps*2.0;
 for(int k=0;k<uFogShadow&&travel<maxDist;++k){
  vec3 q=P+L*travel;float sd,str;bool ok=de(q,sd,str);
  if(!ok||sd<eps)return 0.0;
  vis=min(vis,12.0*sd/travel);
  float stride=max(sd*uSafety,eps);
  @REPEAT@
  travel+=stride;
 }
 return clamp(vis,0.0,1.0);
}
vec3 fogMarch(float T,vec3 origin,vec3 dir,float x,float y,uint seq,out float trans){
 float sigma=uFog;trans=exp(-sigma*T);
 float tm=min(T,16.0/sigma);
 int n=max(1,uFogSteps);
 float ds=tm/float(n),jitter=fogJitter(uint(x),uint(y),seq);
 vec3 light=vec3(0.0);
 for(int i=0;i<n;++i){
  float sd=(float(i)+jitter)*ds,Ts=exp(-sigma*sd);
  if(Ts<1e-4)break;
  vec3 P=origin+dir*sd;float eps=epsAt(P,sd);vec3 incident=vec3(0.0);
  if(uSun>0.0){float vis=fogVis(P,uLight,8.0,eps);if(vis>0.0)incident+=vec3(fogPhase(dot(dir,uLight))*uSun*vis);}
  for(int k=0;k<2;++k){
   if((uPointMask&(1<<k))==0)continue;
   vec3 delta=uPointPos[k]-P;float len=length(delta);
   if(len>=uPointRange[k]||len<=1e-9)continue;
   vec3 L=delta*(1.0/len);float fall=1.0-len/uPointRange[k],power=uPointInt[k]*fall*fall/(1.0+len*len);
   float vis=fogVis(P,L,len,eps);
   if(vis>0.0)incident+=uPointCol[k]*(power*vis*fogPhase(dot(dir,L)));
  }
  vec3 scatter=uFogColor*(incident+vec3(uFogAmbient))*(uFogScatter*sigma);
  light+=scatter*(Ts*ds);
 }
 return light;
}
#endif
)","L","eps");
 // ---- ray marcher
 o+=R"(
// x,y: CPU pixel coordinates (y down). Returns linear radiance; mg flags the invalid-sample colour.
vec3 sampleRay(float x,float y,uint seq,out bool mg){
 mg=false;
 float aspect=uRes.x/uRes.y;
 float px=(2.0*x/uRes.x-1.0)*uTangent*aspect-uEye/uConv;
 float py=(1.0-2.0*y/uRes.y)*uTangent;
 vec3 origin=uOrigin,dir=unit(uForward+uRight*px+uUp*py);
#if F_LENS
 if(uLens!=0){ // thin lens, Halton samples keyed by sequence
  float radius=sqrt(radical(seq+1u,2u)),angle=6.2831853*radical(seq+1u,3u);
  vec3 focus=origin+dir*(uFocus/max(1e-6,dot(dir,uForward)));
  origin+=(uRight*(radius*cos(angle))+uUp*(radius*sin(angle)))*uAperture;
  dir=unit(focus-origin);
 }
#endif
 float t=0.0,trap=0.0,d,tr;bool hit=false;
 float previousT=0.0,previousD=0.0,advance=0.0;bool relaxed=false;
 for(int i=0;i<uSteps;++i){
  if(!(t<uFar))break;
  vec3 pt=origin+dir*t;
  if(!de(pt,d,tr)){mg=true;return vec3(0.0);}
  if(relaxed&&previousD+d<advance){
   t=previousT+max(previousD*uSafety,uEps*0.25);pt=origin+dir*t;
   if(!de(pt,d,tr)){mg=true;return vec3(0.0);}
   relaxed=false;
  }
  float eps=hitTol(t);
  if(d<eps){hit=true;trap=tr;break;}
  previousT=t;previousD=d;advance=max(d*uSafety,eps*0.25);
  relaxed=uRelax>1.0;if(relaxed)advance*=uRelax;
)";
 if(f.repeat)o+="  advance=min(advance,repeatBoundaryStep(pt,dir)+eps*0.25);\n";
 o+=R"(  t+=advance;
 }
 if(!hit){
#if F_FOG
  if(uFogOn!=0){float tr;vec3 fl=fogMarch(uFar,origin,dir,x,y,seq,tr);return (uSky*tr+fl)*uExposure;}
#endif
  return uSky*uExposure;
 }
 vec3 p=origin+dir*t;
 de(p,d,trap);
 float e=epsAt(p,t);
 vec3 n=tetraNormal(p,e);
 float diffuse=max(0.0,dot(n,uLight)),ao=1.0,shadow=1.0;
 float baseSpec=(uSpecular>0.0&&uSun>0.0)?pow(max(0.0,dot(n,unit(uLight-dir))),24.0)*uSpecular:0.0;
 for(int i=1;i<=uAo&&ao>0.15;++i){float h=e*6.0*float(i);ao-=max(0.0,h-dist(p+n*h))/h*(0.35/float(i));}
 if(uSun>0.0&&uShadow!=0&&(diffuse>0.0||baseSpec>0.0)){
  float st=e*8.0;vec3 start=p+n*(e*4.0);
  for(int i=0;i<uShadow&&st<8.0;++i){
   float sd,str;bool ok=de(start+uLight*st,sd,str);
   if(!ok||sd<e){shadow=0.15;break;}
   shadow=min(shadow,12.0*sd/st);st+=max(sd*uSafety,e);
  }
 }
 float spec=baseSpec*shadow*uSun;
 float lighting=clamp(ao,0.15,1.0)*(0.22+0.78*diffuse*clamp(shadow,0.15,1.0)*uSun);
 vec3 albedo=gradientColor(trap);
 vec3 color=albedo*(lighting+uEmission)+vec3(spec);
#if F_POINT
 if(uPointMask!=0){vec3 localD=vec3(0.0),localS=vec3(0.0);pointLighting(p,n,e,dir,true,localD,localS);color+=albedo*localD+localS;}
#endif
#if F_GI
 if(uGiSamples>0&&uGiStrength>0.0)color+=albedo*indirectLight(p,n,e,x,y,seq);
#endif
#if F_FOG
 if(uFogOn!=0){float tr;vec3 fl=fogMarch(t,origin,dir,x,y,seq,tr);return (color*tr+fl)*uExposure;}
#endif
 float fog=exp(-t*uFog);
 return (color*fog+uSky*(1.0-fog))*uExposure;
}

void main(){
 // GL row r (bottom-up) -> CPU row uRes.y-1-r (top-down); see file header.
 float xl=floor(gl_FragCoord.x),rowTop=uRes.y-1.0-floor(gl_FragCoord.y);
 float B=float(uBlock);
 xl=floor(xl/B)*B;rowTop=floor(rowTop/B)*B; // coarse final render: one ray per block cell
 ivec3 intSum=ivec3(0);vec3 radSum=vec3(0.0);
 for(int i=0;i<uCount;++i){
  float jx,jy;pattern(i,uCount,jx,jy);
  if(uJitter!=0&&uCount==1)pattern(uPass,1073741824,jx,jy); // progressive pass jitter, as renderJob()
  bool mg;
  vec3 rad=sampleRay(min(uRes.x-0.5,xl+B*jx),min(uRes.y-0.5,rowTop+B*jy),uint(uPass*uCount+i),mg);
  if(mg){intSum+=ivec3(180,20,90);radSum+=vec3(180.0,20.0,90.0)*(1.0/255.0);}
  else{intSum+=quantize(rad);radSum+=rad;}
 }
 if(uOutMode!=0){outColor=vec4(radSum*(1.0/float(uCount)),1.0);return;} // accumulation pass: linear radiance
 ivec3 q=uLinear!=0?quantize(radSum*(1.0/float(uCount))):intSum/uCount;
 outColor=vec4(vec3(q)/255.0,1.0);
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

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif

namespace {
// Bloom post-process, mirrors Renderer::applyBloom: 13-tap separable box blur of the bright-pass.
const char* kBloomFragment=R"(#version 330 core
layout(location=0) out vec4 outColor;
uniform sampler2D uSrc;uniform sampler2D uBase;uniform vec2 uSizeF;uniform float uBloom;uniform int uVertical;
vec3 bright(vec3 v){float peak=max(v.x,max(v.y,v.z));return v*(peak>0.6?(peak-0.6)/peak:0.0);}
void main(){
 ivec2 p=ivec2(gl_FragCoord.xy);ivec2 uSize=ivec2(uSizeF);
 vec3 s=vec3(0.0);
 if(uVertical==0){
  for(int dx=-6;dx<=6;++dx)s+=bright(texelFetch(uSrc,ivec2(clamp(p.x+dx,0,uSize.x-1),p.y),0).rgb);
  outColor=vec4(s*(1.0/13.0),1.0);
 }else{
  for(int dy=-6;dy<=6;++dy)s+=texelFetch(uSrc,ivec2(p.x,clamp(p.y+dy,0,uSize.y-1)),0).rgb;
  vec3 v=texelFetch(uBase,p,0).rgb+s*(uBloom/13.0);
  outColor=vec4(vec3(ivec3(clamp(v,0.0,1.0)*255.0))/255.0,1.0);
 }
}
)";
bool lightingProgressive(const Settings& s){return s.progressiveLighting&&((s.giSamples>0&&s.giStrength>0)||(s.dof&&s.aperture>0));}
// Auto refine: the finished CPU image is the mean of refineSamples jittered passes (R2 sub-pixel positions).
bool refineActive(const Settings& s){return s.autoRefine&&!s.quality&&s.refineSamples>1&&!lightingProgressive(s);}
bool volumetricActive(const Settings& s){return s.volumetricFog&&s.fog>0&&std::isfinite(s.fog);}
bool separateActive(const Settings& s){return !volumetricActive(s)&&s.separateLighting&&!s.quality&&!(s.dof&&s.aperture>0)&&s.giSamples>0&&s.giStrength>0;}
// Passes the finished image is the mean of (1 = single draw straight into the display target).
int passCount(const Settings& s){return lightingProgressive(s)?std::max(1,s.lightingPasses):refineActive(s)?std::max(1,s.refineSamples):1;}
unsigned featuresOf(const Scene& scene){
 const Settings& o=scene.settings;unsigned f=0;
 if(o.giSamples>0&&o.giStrength>0)f|=FeatGI;
 if(volumetricActive(o))f|=FeatFog;
 for(const PointLight& l:o.pointLights)if(l.enabled&&l.intensity>0)f|=FeatPoint;
 if(o.dof&&o.aperture>0)f|=FeatLens;
 return f;
}
const char* kResolveFragment=R"(#version 330 core
layout(location=0) out vec4 outColor;
uniform sampler2D uAccum;uniform float uInv;
void main(){
 vec3 v=texelFetch(uAccum,ivec2(gl_FragCoord.xy),0).rgb*uInv;
 outColor=vec4(vec3(ivec3(clamp(v,0.0,1.0)*255.0))/255.0,1.0);
}
)";
// Cell size of the finished CPU image (the progressive refinement ends at this block).
int finalBlock(const Settings& s){return s.quality?1:std::max(1,s.autoRefine?s.stillBlock:std::max(s.previewBlock,s.stillBlock));}
}

struct GpuRenderer::Impl {
 std::map<std::string,Program> programs; // keyed by generated fragment source
 Program* current=nullptr;
 Program bloomProgram;
 GLuint vao=0;
 GLuint tex[2]={0,0},fbo[2]={0,0};       // presented image per eye
 GLuint baseTex[2]={0,0},baseFbo[2]={0,0}; // scene render target while bloom is on
 GLuint tmpTex=0,tmpFbo=0;                // bloom horizontal pass (RGBA16F)
 GLuint accumTex[2]={0,0},accumFbo[2]={0,0}; // running sum of pass radiance (RGBA32F), only when passes>1
 Program resolveProgram;
 int totalPasses=1,passIndex[2]={0,0};bool accumMode=false;
 int width=0,height=0;
 int eyeCount=1;float eyeOffset=0;
 bool active=false;
 bool bloomOn=false,bloomDone[2]={false,false};
 int renderW=0,renderH=0;      // region of the image being / last rendered
 int rowsDone[2]={0,0},colsDone[2]={0,0};
 double msPerPixel=0;int lastPixels=64;
 std::string renderer;
 bool inited=false;

 void destroyTargets(){
  for(int e=0;e<2;++e){
   if(fbo[e]){glDeleteFramebuffers(1,&fbo[e]);fbo[e]=0;}
   if(tex[e]){glDeleteTextures(1,&tex[e]);tex[e]=0;}
   if(baseFbo[e]){glDeleteFramebuffers(1,&baseFbo[e]);baseFbo[e]=0;}
   if(baseTex[e]){glDeleteTextures(1,&baseTex[e]);baseTex[e]=0;}
  }
  for(int e=0;e<2;++e){
   if(accumFbo[e]){glDeleteFramebuffers(1,&accumFbo[e]);accumFbo[e]=0;}
   if(accumTex[e]){glDeleteTextures(1,&accumTex[e]);accumTex[e]=0;}
  }
  if(tmpFbo){glDeleteFramebuffers(1,&tmpFbo);tmpFbo=0;}
  if(tmpTex){glDeleteTextures(1,&tmpTex);tmpTex=0;}
 }
 bool makeTarget(GLuint& t,GLuint& fb,GLenum internal,GLenum type,GLenum filter){
  glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
  glTexImage2D(GL_TEXTURE_2D,0,GLint(internal),width,height,0,GL_RGBA,type,nullptr);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GLint(filter));
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GLint(filter));
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
  bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
  return ok;
 }
 // Scene target of an eye: the bloom base while bloom is pending, the presented image otherwise.
 GLuint sceneFbo(int e)const{return bloomOn?baseFbo[e]:fbo[e];}
 GLuint shownTex(int e)const{return bloomOn&&!bloomDone[e]?baseTex[e]:tex[e];}
 GLuint shownFbo(int e)const{return bloomOn&&!bloomDone[e]?baseFbo[e]:fbo[e];}
 bool ensureBloom(std::string& error){
  if(!baseTex[0]){
   for(int e=0;e<2;++e)if(!makeTarget(baseTex[e],baseFbo[e],GL_RGBA8,GL_UNSIGNED_BYTE,GL_LINEAR)){error="bloom targets";return false;}
   if(!makeTarget(tmpTex,tmpFbo,GL_RGBA16F,GL_HALF_FLOAT,GL_NEAREST)){error="bloom targets";return false;}
   glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
  }
  if(!bloomProgram.id&&!build(bloomProgram,kBloomFragment,error))return false;
  return true;
 }
 bool ensureAccum(std::string& error){
  if(!accumTex[0]){
   for(int e=0;e<2;++e)if(!makeTarget(accumTex[e],accumFbo[e],GL_RGBA32F,GL_FLOAT,GL_NEAREST)){error="accumulation targets";return false;}
   glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
  }
  if(!resolveProgram.id&&!build(resolveProgram,kResolveFragment,error))return false;
  return true;
 }
 void resolve(int e,int passes){ // display = quantize(sum/passes), restricted to the rendered region
  glUseProgram(resolveProgram.id);glDisable(GL_BLEND);glEnable(GL_SCISSOR_TEST);glScissor(0,0,renderW,renderH);
  glBindFramebuffer(GL_FRAMEBUFFER,sceneFbo(e));glViewport(0,0,width,height);
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,accumTex[e]);
  glUniform1i(resolveProgram.loc("uAccum"),0);glUniform1f(resolveProgram.loc("uInv"),1.f/float(passes));
  glDrawArrays(GL_TRIANGLES,0,3);glBindTexture(GL_TEXTURE_2D,0);
 }
 void applyBloom(int e,float strength);
 void setUniforms(const Scene& scene,int rw,int rh);
};

GpuRenderer::GpuRenderer(){impl=new Impl;}
GpuRenderer::~GpuRenderer(){delete impl;}

bool GpuRenderer::supports(const Scene& scene,std::string& reason){
 const Formula& f=scene.formula;const Settings& o=scene.settings;
 auto no=[&](const char* why){reason=why;return false;};
 if(f.activeCount<1||f.activeCount>MaxStages)return no("formula has no valid active stage list");
 int exprCode=0;
 for(int i=0;i<f.activeCount;++i){const Stage& st=f.stages[f.active[i]];
  if(int(st.kind)<0||st.kind>=Kind::Count)return no("unknown stage kind");
  if(st.kind==Kind::Expression)for(int j=0;j<3;++j){exprCode+=st.expr[j].count;if(st.expr[j].stack>32)return no("expression stack above 32");}
 }
 if(SDL_GL_GetCurrentContext()){ // each stage needs 3 vec4 + a mat3 (3 vec4) of uniforms
  GLint comps=0;glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,&comps);
  if(comps>0&&(6*f.activeCount+48)*4>comps)return no("stage count exceeds the GPU uniform limit");
 }
 if(exprCode>1200)return no("expression stages too large for the shader generator");
 if(o.deepDouble)return no("deep zoom needs the double-precision CPU path");
 if(o.upscale!=0&&finalBlock(o)>1)return no("upscale filter on a coarse final render");
 reason.clear();return true;
}

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
 if(impl->resolveProgram.id)glDeleteProgram(impl->resolveProgram.id);
 impl->resolveProgram=Program();
 if(impl->bloomProgram.id)glDeleteProgram(impl->bloomProgram.id);
 impl->bloomProgram=Program();
 if(impl->vao)glDeleteVertexArrays(1,&impl->vao);
 impl->vao=0;impl->width=impl->height=0;impl->active=false;impl->inited=false;
}

bool GpuRenderer::resize(int w,int h){
 if(w<1||h<1)return false;
 if(w==impl->width&&h==impl->height&&impl->tex[0])return true;
 impl->destroyTargets();
 impl->width=w;impl->height=h;
 for(int e=0;e<2;++e){
  if(!impl->makeTarget(impl->tex[e],impl->fbo[e],GL_RGBA8,GL_UNSIGNED_BYTE,GL_LINEAR)){
   glBindFramebuffer(GL_FRAMEBUFFER,0);impl->destroyTargets();impl->width=impl->height=0;return false;}
 }
 glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
 impl->active=false;
 return true;
}

bool GpuRenderer::begin(const Scene& scene,float eyeOffset,std::string& error){
 Impl& m=*impl;m.active=false;
 const Formula& f=scene.formula;
 if(f.activeCount<1||f.activeCount>MaxStages){error="formula has no active stages (call Formula::validate)";return false;}
 if(m.width<1){error="resize() has not been called";return false;}
 std::string src=generateFragment(f,featuresOf(scene));
 auto it=m.programs.find(src);
 if(it==m.programs.end()){
  Program prog;if(!build(prog,src,error))return false;
  // Keep the cache small; never drop the program currently in use.
  while(m.programs.size()>=6){auto victim=m.programs.begin();if(&victim->second==m.current)++victim;
   if(victim==m.programs.end())break;glDeleteProgram(victim->second.id);m.programs.erase(victim);}
  it=m.programs.emplace(src,std::move(prog)).first;
  m.msPerPixel=0;m.lastPixels=64; // new shader: re-measure cost
  // Warm-up draw (all loop counts are still 0): moves driver-side compilation out of the timed strips.
  glUseProgram(it->second.id);glBindVertexArray(m.vao);glBindFramebuffer(GL_FRAMEBUFFER,m.fbo[0]);
  glViewport(0,0,m.width,m.height);glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
  glDrawArrays(GL_TRIANGLES,0,3);glFinish();glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glUseProgram(0);
 }
 m.current=&it->second;
 m.totalPasses=passCount(scene.settings);m.accumMode=m.totalPasses>1;
 if(m.accumMode&&!m.ensureAccum(error))return false;
 m.passIndex[0]=m.passIndex[1]=0;
 m.bloomOn=scene.settings.bloom>0;
 if(m.bloomOn&&!m.ensureBloom(error)){m.bloomOn=false;return false;}
 m.eyeOffset=eyeOffset;m.eyeCount=eyeOffset==0?1:2;
 m.rowsDone[0]=m.rowsDone[1]=0;m.colsDone[0]=m.colsDone[1]=0;m.bloomDone[0]=m.bloomDone[1]=false;
 m.renderW=m.renderH=0;
 m.active=true;
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
 u1f("uExposure",o.exposure);u1f("uSpecular",o.specular);u1f("uSun",o.sunStrength);u1f("uEmission",o.emission);
 u1f("uRelax",(!o.quality&&!f.repeat&&o.relaxation>1)?o.relaxation:1.f);
 u1i("uFogOn",volumetricActive(o)?1:0);u1i("uFogSteps",std::max(1,o.fogSteps));u1i("uFogShadow",std::max(0,o.fogShadowSteps));
 u1f("uFogG",o.fogAnisotropy);u1f("uFogAmbient",o.fogAmbient);u1f("uFogScatter",o.fogScatter);u3f("uFogColor",o.fogColor);
 u1i("uQuality",o.quality?1:0);u1i("uSteps",int(std::min<long long>(INT_MAX,o.quality?(long long)o.steps*2:(long long)o.steps)));
 u1i("uAo",std::max(0,o.ao));u1i("uShadow",std::max(0,o.shadow));
 u1i("uAdaptive",o.adaptivePrecision?1:0);u1f("uMinEps",o.minEpsilon);u1f("uPixTol",o.pixelTolerance);
 u3f("uSky",o.skyColor);
 // Sampling plan, mirroring renderJob() for a finished image.
 bool lens=o.dof&&o.aperture>0,prog=lightingProgressive(o),refine=refineActive(o),split=separateActive(o);
 int count=o.quality?std::max(1,o.samples):1;
 if(lens)count=std::max(count,std::max(1,o.dofSamples));
 bool accumulate=(prog||refine)&&!split;
 u1i("uCount",count);u1i("uLens",lens?1:0);u1f("uAperture",o.aperture);u1f("uFocus",o.focusDistance);
 u1i("uVaried",(lens||accumulate||split)?1:0);u1i("uLinear",(lens||accumulate)?1:0);u1i("uOutMode",accumMode?1:0);
 u1i("uJitter",accumulate?1:0);
 u1i("uBlock",finalBlock(o));
 u1i("uGiSamples",std::max(0,o.giSamples));u1i("uGiSteps",std::max(0,o.giSteps));
 u1f("uGiStrength",o.giStrength);u1f("uGiRange",o.giRange);
 // point lights
 float pos[6],col[6],inten[2],range[2];
 for(int i=0;i<2;++i){const PointLight& l=o.pointLights[i];Vec q=rays.pointPositions[i];
  pos[i*3]=q.x;pos[i*3+1]=q.y;pos[i*3+2]=q.z;col[i*3]=l.color.x;col[i*3+1]=l.color.y;col[i*3+2]=l.color.z;
  inten[i]=l.intensity;range[i]=l.range;}
 u1i("uPointMask",rays.pointMask);u1i("uPointShadows",o.pointShadows?1:0);
 glUniform3fv(p.loc("uPointPos"),2,pos);glUniform3fv(p.loc("uPointCol"),2,col);
 glUniform1fv(p.loc("uPointInt"),2,inten);glUniform1fv(p.loc("uPointRange"),2,range);
 // palette: same selection as gradientColor()
 Vec lo=o.gradientLow,hi=o.gradientHigh;
 if(!o.customGradient){int n=int(o.palette)%4;
  if(n==1){lo={.13f,.4f,.48f};hi={.72f,.85f,.55f};}
  else if(n==2){lo={.3f,.09f,.36f};hi={.95f,.43f,.63f};}
  else if(n==3){lo={.18f,.19f,.17f};hi={.85f,.86f,.73f};}
  else{lo={.18f,.06f,.025f};hi={.95f,.58f,.22f};}
 }
 float mid[9];for(int i=0;i<3;++i){mid[i*3]=o.gradientMiddle[i].x;mid[i*3+1]=o.gradientMiddle[i].y;mid[i*3+2]=o.gradientMiddle[i].z;}
 u3f("uGradLo",lo);u3f("uGradHi",hi);glUniform3fv(p.loc("uGradMid"),3,mid);
 u1f("uGradScale",o.gradientScale);u1f("uGradOffset",o.gradientOffset);
 u1i("uGradRepeat",o.gradientRepeat?1:0);u1i("uGradBounded",o.boundedGradient?1:0);
 u1i("uGradStops",o.customGradient&&o.gradientStops>2?std::min(5,o.gradientStops):2);
 // The caller supplies the iteration count to render with (see renderingIterations()).
 u1i("uIter",f.iterations);u1f("uBail2",float(std::min(double(f.bailout)*double(f.bailout),3.0e38)));u1f("uTermR",f.terminalRadius);u1f("uDScale",f.derivativeScale);
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

void GpuRenderer::Impl::applyBloom(int e,float strength){
 glUseProgram(bloomProgram.id);glBindVertexArray(vao);
 glViewport(0,0,width,height);glEnable(GL_SCISSOR_TEST);glScissor(0,0,renderW,renderH);
 glUniform2f(bloomProgram.loc("uSizeF"),float(renderW),float(renderH));glUniform1f(bloomProgram.loc("uBloom"),strength);
 glUniform1i(bloomProgram.loc("uSrc"),0);glUniform1i(bloomProgram.loc("uBase"),1);
 glActiveTexture(GL_TEXTURE0+1);glBindTexture(GL_TEXTURE_2D,baseTex[e]);
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,baseTex[e]);
 glUniform1i(bloomProgram.loc("uVertical"),0);
 glBindFramebuffer(GL_FRAMEBUFFER,tmpFbo);glDrawArrays(GL_TRIANGLES,0,3);
 glBindTexture(GL_TEXTURE_2D,tmpTex);
 glUniform1i(bloomProgram.loc("uVertical"),1);
 glBindFramebuffer(GL_FRAMEBUFFER,fbo[e]);glDrawArrays(GL_TRIANGLES,0,3);
 glFinish();
 glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0+1);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);
 glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glUseProgram(0);
 bloomDone[e]=true;
}

bool GpuRenderer::render(const Scene& scene,int renderW,int renderH,float budgetMs){
 Impl& m=*impl;
 if(!m.active||!m.current)return true;
 renderW=std::max(1,std::min(renderW,m.width));renderH=std::max(1,std::min(renderH,m.height));
 if(m.renderW!=renderW||m.renderH!=renderH){ // region changed mid-image: restart it
  m.renderW=renderW;m.renderH=renderH;m.rowsDone[0]=m.rowsDone[1]=0;m.colsDone[0]=m.colsDone[1]=0;
  m.passIndex[0]=m.passIndex[1]=0;m.bloomDone[0]=m.bloomDone[1]=false;}
 auto t0=Clock::now();
 auto enter=[&](){glUseProgram(m.current->id);glBindVertexArray(m.vao);
  glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_SCISSOR_TEST);glViewport(0,0,m.width,m.height);};
 enter();
 m.setUniforms(scene,renderW,renderH);
 Rays rays(scene);
 // Each draw call is sized to ~target ms (rows when cheap, pixel runs of one row when a single row is too slow).
 const double target=std::max(2.0,std::min(12.0,double(budgetMs)));
 bool complete=true;
 for(;;){
  // Work on the eye that is furthest behind, so a progressive image refines both eyes evenly.
  int e=-1;for(int k=0;k<m.eyeCount;++k)if(m.passIndex[k]<m.totalPasses&&(e<0||m.passIndex[k]<m.passIndex[e]))e=k;
  if(e<0)break;
  double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
  if(elapsed>=budgetMs){complete=false;break;}
  const int pass=m.passIndex[e];
  float eye=m.eyeCount==1?0.f:(e?m.eyeOffset:-m.eyeOffset);
  Vec origin=Vec(scene.camera.position+rays.right*eye);
  glBindFramebuffer(GL_FRAMEBUFFER,m.accumMode?m.accumFbo[e]:m.sceneFbo(e));
  if(m.accumMode&&pass>0){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);}else glDisable(GL_BLEND);
  glUniform1f(m.current->loc("uEye"),eye);glUniform1i(m.current->loc("uPass"),pass);
  glUniform3f(m.current->loc("uOrigin"),origin.x,origin.y,origin.z);
  double wanted=m.msPerPixel>0?target/m.msPerPixel:double(m.lastPixels);
  int budget=int(std::max(1.0,std::min({wanted,double(m.lastPixels)*2.0,double(renderW)*renderH})));
  int x0=m.colsDone[e],y0=m.rowsDone[e],w,rows;
  if(x0==0&&budget>=renderW){w=renderW;rows=std::max(1,std::min(budget/renderW,renderH-y0));}
  else{w=std::min(renderW-x0,budget);rows=1;}
  glScissor(x0,y0,w,rows);
  auto s0=Clock::now();
  glDrawArrays(GL_TRIANGLES,0,3);glFinish();
  double ms=std::chrono::duration<double,std::milli>(Clock::now()-s0).count();
  int pixels=w*rows;double per=ms/pixels;
  m.msPerPixel=m.msPerPixel>0?std::max(per,m.msPerPixel*0.8):per; // react fast to slowdowns, relax slowly
  m.lastPixels=pixels;
  if(x0+w>=renderW){m.colsDone[e]=0;m.rowsDone[e]+=rows;}else m.colsDone[e]=x0+w;
  if(m.rowsDone[e]>=renderH){ // pass finished: publish the running mean (complete, upright image)
   m.rowsDone[e]=0;m.colsDone[e]=0;++m.passIndex[e];
   glDisable(GL_BLEND);
   if(m.accumMode)m.resolve(e,m.passIndex[e]);
   if(m.bloomOn){glDisable(GL_SCISSOR_TEST);m.applyBloom(e,scene.settings.bloom);}
   enter();
  }
 }
 glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);glUseProgram(0);
 return complete;
}

int GpuRenderer::passesTotal() const{return impl->totalPasses;}
int GpuRenderer::passesDone() const{return impl->eyeCount==2?std::min(impl->passIndex[0],impl->passIndex[1]):impl->passIndex[0];}
int GpuRenderer::eyes() const{return impl->eyeCount;}
unsigned GpuRenderer::texture(int eye) const{return impl->shownTex(impl->eyeCount==1?0:std::max(0,std::min(1,eye)));}
const char* GpuRenderer::rendererName() const{return impl->renderer.c_str();}

bool GpuRenderer::readback(int eye,std::vector<Color>& out,int& width,int& height){
 Impl& m=*impl;
 int e=m.eyeCount==1?0:std::max(0,std::min(1,eye));
 int w=m.renderW?m.renderW:m.width,h=m.renderH?m.renderH:m.height;
 GLuint fb=m.shownFbo(e);
 if(!fb||w<1||h<1)return false;
 std::vector<uint8_t> raw(size_t(w)*h*4);
 glFinish();glBindFramebuffer(GL_FRAMEBUFFER,fb);glPixelStorei(GL_PACK_ALIGNMENT,1);
 glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,raw.data());
 glBindFramebuffer(GL_FRAMEBUFFER,0);
 out.resize(size_t(w)*h);
 for(int y=0;y<h;++y){const uint8_t* src=&raw[size_t(h-1-y)*w*4];
  for(int x=0;x<w;++x)out[size_t(y)*w+x]={src[x*4],src[x*4+1],src[x*4+2]};}
 width=w;height=h;return true;
}
}
