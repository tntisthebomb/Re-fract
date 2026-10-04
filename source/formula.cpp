#include "engine.hpp"
#include <algorithm>

namespace rf {
const char* kindName(Kind k){
 const char* names[]={"BOX FOLD","SPHERE FOLD","BULB POWER","SCALE + C","ROTATE XYZ","OFFSET","ABSOLUTE","SORT XYZ","MENGER","TETRA FOLD","EXPRESSION"};
 int n=int(k);return n>=0&&n<int(Kind::Count)?names[n]:"INVALID";
}
bool Stage::compile(std::string& error){
 minimum2=a*a;fixed2=b*b;innerScale=fixed2/std::fmax(minimum2,1e-12f);
 thetaPower=a*b;phiPower=a*c;stretch=a*std::fmax(1.f,std::fmax(std::fabs(b),std::fabs(c)));
 integerPower=a>=1&&a<=16&&a==std::floor(a)?int(a)-1:-1;
 if(kind==Kind::Rotate){
  float ca=std::cos(a),sa=std::sin(a),cb=std::cos(b),sb=std::sin(b),cc=std::cos(c),sc=std::sin(c);
  rotation={{cc*cb,cc*sb*sa-sc*ca,cc*sb*ca+sc*sa,sc*cb,sc*sb*sa+cc*ca,sc*sb*ca-cc*sa,-sb,cb*sa,cb*ca}};
 }
 if(kind==Kind::Expression)for(int i=0;i<3;++i)if(!expr[i].compile(text[i],error)){error=std::string("XYZ").substr(i,1)+": "+error;return false;}
 return true;
}
bool Formula::validate(std::string& error){
 if(stages.empty()||stages.size()>MaxStages){error="Use 1 to 12 stages";return false;}
 if(iterations<1||iterations>32||!std::isfinite(bailout)||bailout<2||bailout>256||
 !std::isfinite(derivativeScale)||derivativeScale<1||derivativeScale>100||terminal<0||terminal>2||
 !std::isfinite(terminalRadius)||terminalRadius<.01f||terminalRadius>100){error="Invalid formula limits";return false;}
 if(!std::isfinite(constant.x)||!std::isfinite(constant.y)||!std::isfinite(constant.z)){error="Invalid Julia constant";return false;}
 auto periodValid=[](float v){return std::isfinite(v)&&(v==0||(v>=.01f&&v<=1000));};
 if(!periodValid(repeatPeriod.x)||!periodValid(repeatPeriod.y)||!periodValid(repeatPeriod.z)){error="Repeat spacing: 0 or 0.01..1000";return false;}
 bool enabled=false;
 for(Stage& s:stages){enabled|=s.enabled;
  if(int(s.kind)<0||s.kind>=Kind::Count||!std::isfinite(s.a)||!std::isfinite(s.b)||!std::isfinite(s.c)||
    std::fabs(s.a)>100||std::fabs(s.b)>100||std::fabs(s.c)>100){error="Invalid stage parameter";return false;}
  if(s.kind==Kind::Bulb&&(s.a<1.1f||s.a>16||std::fabs(s.b)>4||std::fabs(s.c)>4)){error="Bulb: power 1.1..16, angles -4..4";return false;}
  if(s.kind==Kind::SphereFold&&(s.a<=.0001f||s.b<s.a)){error="Sphere radii: 0 < minimum <= fixed";return false;}
  if(s.kind==Kind::Menger&&s.a<1.1f){error="Menger scale must exceed 1.1";return false;}
  if(!s.compile(error))return false;
 }
 if(!enabled){error="Enable at least one stage";return false;}
 activeCount=0;for(size_t i=0;i<stages.size();++i)if(stages[i].enabled)active[activeCount++]=int(i);
 error.clear();return true;
}
namespace {
Stage stage(Kind k,float a=0,float b=0,float c=0){Stage s;s.kind=k;s.a=a;s.b=b;s.c=c;return s;}
float radialPower(float r,float p,int n){
 if(n>=0){float out=1;while(n){if(n&1)out*=r;r*=r;n>>=1;}return out;}
 return std::pow(r,p);
}
}
const char* presetName(int n){
 const char* names[]={"MANDELBULB / 8","MANDELBULB / 2","MANDELBULB / 3","MANDELBULB / 6","MANDELBULB / 12","JULIA BULB","MANDELBOX","NEGATIVE BOX","CORRIDOR BOX","SPHERE NETWORK","MENGER SPONGE","TWISTED MENGER","SIERPINSKI TETRA","KALEIDO TETRA","BOX-BULB HYBRID","FOLDED JULIA BOX","EXPRESSION JULIA","INVERTED BOX"};
 return names[(n%PresetCount+PresetCount)%PresetCount];
}
Camera presetCamera(int n){
 Camera c;
 c.position.z=(n>=6&&n<=9)||n==15||n==17?-9.f:n==14?-7.f:-3.f;
 if(n==9){c.position.z=-14;c.position.y=.5f;}
 return c;
}
Formula preset(int n){
 n=(n%PresetCount+PresetCount)%PresetCount;Formula f;
 if(n<=5){float powers[]={8,2,3,6,12,8};f.logarithmic=true;f.iterations=9;f.stages={stage(Kind::Bulb,powers[n],1,1),stage(Kind::Scale,1,1)};
  if(n==5){f.julia=true;f.constant={.25f,-.15f,.1f};}
 }else if(n<=9||n==15||n==17){
  f.iterations=12;f.bailout=32;
  float scale=n==7?-1.5f:n==8?2.8f:n==9?1.4f:2;
  f.stages={stage(Kind::BoxFold,1),stage(Kind::SphereFold,n==9?.15f:.5f,1),stage(Kind::Scale,scale,1)};
  if(n==15){f.julia=true;f.constant={.1f,.2f,-.1f};}
  if(n==17){f.stages[1].a=.12f;f.stages[2].a=-1.8f;}
 }else if(n==10||n==11){
  f.iterations=6;f.bailout=100;f.terminal=1;f.stages={stage(Kind::Menger,3,1,1)};
  if(n==11)f.stages.insert(f.stages.begin(),stage(Kind::Rotate,.1f,.2f,.08f));
 }else if(n==12||n==13){
  f.iterations=10;f.bailout=100;f.terminal=2;f.stages={stage(Kind::Tetra),stage(Kind::Scale,2,0),stage(Kind::Offset,-1,-1,-1)};
  if(n==13)f.stages.insert(f.stages.begin(),stage(Kind::Rotate,.05f,.12f,0));
 }else if(n==14){f.logarithmic=true;f.iterations=8;f.stages={stage(Kind::BoxFold,1),stage(Kind::Bulb,3,1,1),stage(Kind::Scale,1,1)};
 }else if(n==16){
  f.julia=true;f.constant={-.3f,.2f,.1f};f.stages={stage(Kind::Expression)};
  f.stages[0].text={{"x*x-y*y-z*z+cx","2*x*y+cy","2*x*z+cz"}};
  f.logarithmic=true;
 }
 std::string error;f.validate(error);return f;
}
template<bool Trap> Sample evaluateDistance(const Formula& f,Vec p){
 Vec z=p,c=f.julia?f.constant:p;float dr=1,trap=1e6f,r=0;
 for(int i=0;i<f.iterations;++i){
  if(z.dot(z)>f.bailout*f.bailout)break;
  for(int stageIndex=0;stageIndex<f.activeCount;++stageIndex){const Stage& s=f.stages[f.active[stageIndex]];
   switch(s.kind){
    case Kind::BoxFold:{float limit=std::fabs(s.a);z={2*clamp(z.x,-limit,limit)-z.x,2*clamp(z.y,-limit,limit)-z.y,2*clamp(z.z,-limit,limit)-z.z};break;}
    case Kind::SphereFold:{float r2=z.dot(z),min2=s.minimum2,fixed2=s.fixed2;
     float k=r2<min2?s.innerScale:r2<fixed2?fixed2/std::fmax(r2,1e-12f):1;
     z=z*k;dr*=k;break;
    }
    case Kind::Bulb:{
     r=z.length();if(r<1e-12f){z={};dr=std::fmax(dr,1e-12f);break;}
     float theta=std::acos(clamp(z.z/r,-1,1))*s.thetaPower,phi=std::atan2(z.y,z.x)*s.phiPower;
     float power=radialPower(r,s.a-1,s.integerPower);
     // Angular multipliers can stretch the transform beyond the standard bulb.
     dr*=power*s.stretch;
     float nr=power*r,sinTheta=std::sin(theta);
     z={nr*sinTheta*std::cos(phi),nr*sinTheta*std::sin(phi),nr*std::cos(theta)};break;
    }
    case Kind::Scale:z=z*s.a+c*s.b;dr=dr*std::fabs(s.a)+(f.julia?0:std::fabs(s.b));break;
    case Kind::Rotate:{const auto& m=s.rotation;z={m[0]*z.x+m[1]*z.y+m[2]*z.z,m[3]*z.x+m[4]*z.y+m[5]*z.z,m[6]*z.x+m[7]*z.y+m[8]*z.z};break;}
    case Kind::Offset:z=z+Vec{s.a,s.b,s.c};break;
    case Kind::Absolute:z={std::fabs(z.x),std::fabs(z.y),std::fabs(z.z)};break;
    case Kind::Sort:if(z.x<z.y)std::swap(z.x,z.y);if(z.x<z.z)std::swap(z.x,z.z);if(z.y<z.z)std::swap(z.y,z.z);break;
    case Kind::Menger:{
     z={std::fabs(z.x),std::fabs(z.y),std::fabs(z.z)};
     if(z.x<z.y)std::swap(z.x,z.y);
     if(z.x<z.z)std::swap(z.x,z.z);
     if(z.y<z.z)std::swap(z.y,z.z);
     z=z*s.a-Vec{s.b,s.b,0}*(s.a-1);
     float cut=s.c*(s.a-1);if(z.z>cut*.5f)z.z-=cut;dr*=std::fabs(s.a);break;
    }
    case Kind::Tetra:
     if(z.x+z.y<0){float t=z.x;z.x=-z.y;z.y=-t;}
     if(z.x+z.z<0){float t=z.x;z.x=-z.z;z.z=-t;}
     if(z.y+z.z<0){float t=z.y;z.y=-z.z;z.z=-t;}break;
    case Kind::Expression:{
     Dual out[3];for(int j=0;j<3;++j)if(!s.expr[j].evaluate(z,c,out[j]))return {0,trap,false};
     // Frobenius norms upper-bound the local Jacobian spectral norms.
     float jz=0,jc=0;for(const Dual& v:out)for(int j=0;j<3;++j){jz+=v.d[j]*v.d[j];jc+=v.d[j+3]*v.d[j+3];}
     dr=std::sqrt(jz)*dr+(f.julia?0:std::sqrt(jc));z={out[0].value,out[1].value,out[2].value};break;
    }
    default:return {0,0,false};
   }
   if(!std::isfinite(z.x)||!std::isfinite(z.y)||!std::isfinite(z.z)||!std::isfinite(dr)||dr>1e30f)return {0,trap,false};
   if constexpr(Trap)trap=std::fmin(trap,z.dot(z));
  }
 }
 r=z.length();float d=f.logarithmic?.5f*std::log(std::fmax(r,1e-12f))*r/std::fmax(dr,1e-12f):r/std::fmax(dr,1e-12f);
 if(f.terminal==1){Vec q{std::fabs(z.x)-f.terminalRadius,std::fabs(z.y)-f.terminalRadius,std::fabs(z.z)-f.terminalRadius};
  Vec outside{std::fmax(q.x,0.f),std::fmax(q.y,0.f),std::fmax(q.z,0.f)};
  d=(outside.length()+std::fmin(std::fmax(q.x,std::fmax(q.y,q.z)),0.f))/std::fmax(dr,1e-12f);
 }else if(f.terminal==2){
  d=(std::fmax(-z.x-z.y-z.z,std::fmax(-z.x+z.y+z.z,std::fmax(z.x-z.y+z.z,z.x+z.y-z.z)))-f.terminalRadius)*.57735027f/std::fmax(dr,1e-12f);
 }
 d=std::fmax(0.f,d)/f.derivativeScale;
 return {d,Trap?std::sqrt(trap):0.f,std::isfinite(d)};
}
namespace {
Vec repeatPoint(const Formula& f,Vec p){if(!f.repeat)return p;auto wrap=[](float v,float period){return period>0?v-period*std::floor(v/period+.5f):v;};return {wrap(p.x,f.repeatPeriod.x),wrap(p.y,f.repeatPeriod.y),wrap(p.z,f.repeatPeriod.z)};}
}
float repeatBoundaryStep(const Formula& f,Vec point,Vec direction){
 if(!f.repeat)return 1e30f;
 Vec local=repeatPoint(f,point);float step=1e30f;
 auto axis=[&](float p,float d,float period){if(period<=0||std::fabs(d)<1e-12f)return;float boundary=d>0?period*.5f:-period*.5f;step=std::fmin(step,std::fmax(0.f,(boundary-p)/d));};
 axis(local.x,direction.x,f.repeatPeriod.x);axis(local.y,direction.y,f.repeatPeriod.y);axis(local.z,direction.z,f.repeatPeriod.z);return step;
}
Sample distance(const Formula& f,Vec p){return evaluateDistance<true>(f,repeatPoint(f,p));}
Sample distanceOnly(const Formula& f,Vec p){return evaluateDistance<false>(f,repeatPoint(f,p));}
}
