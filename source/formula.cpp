#include "engine.hpp"
#include <algorithm>

namespace rf {
const char* kindName(Kind k){
 const char* names[]={"BOX FOLD","SPHERE FOLD","BULB POWER","SCALE + C","ROTATE XYZ","OFFSET","ABSOLUTE","SORT XYZ","MENGER","TETRA FOLD","EXPRESSION","KLEINIAN FOLD","SPHERE INVERSION"};
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
 if(iterations<1||iterations>256||!std::isfinite(bailout)||bailout<2||bailout>1000000||
 !std::isfinite(derivativeScale)||derivativeScale<.001f||derivativeScale>10000||terminal<0||terminal>3||
 !std::isfinite(terminalRadius)||terminalRadius<.000001f||terminalRadius>10000){error="Invalid formula limits";return false;}
 if(!std::isfinite(constant.x)||!std::isfinite(constant.y)||!std::isfinite(constant.z)){error="Invalid Julia constant";return false;}
 auto periodValid=[](float v){return std::isfinite(v)&&(v==0||(v>=.01f&&v<=1000));};
 if(!periodValid(repeatPeriod.x)||!periodValid(repeatPeriod.y)||!periodValid(repeatPeriod.z)){error="Repeat spacing: 0 or 0.01..1000";return false;}
 bool enabled=false;
 for(Stage& s:stages){enabled|=s.enabled;
  if(int(s.kind)<0||s.kind>=Kind::Count||!std::isfinite(s.a)||!std::isfinite(s.b)||!std::isfinite(s.c)||
    std::fabs(s.a)>10000||std::fabs(s.b)>10000||std::fabs(s.c)>10000){error="Invalid stage parameter";return false;}
  if(s.kind==Kind::Bulb&&(s.a<1.01f||s.a>32||std::fabs(s.b)>32||std::fabs(s.c)>32)){error="Bulb: power 1.01..32, angles -32..32";return false;}
  if(s.kind==Kind::SphereFold&&(s.a<=.000001f||s.b<s.a)){error="Sphere radii: 0 < minimum <= fixed";return false;}
  if(s.kind==Kind::KleinianFold&&(s.a<=0||s.b<=0||s.c<=0)){error="Kleinian fold extents must be positive";return false;}
  if(s.kind==Kind::Inversion&&(s.a<.000001f||s.a>1000)){error="Inversion radius: 0.000001..1000";return false;}
  if(s.kind==Kind::Menger&&s.a<1.1f){error="Menger scale must exceed 1.1";return false;}
  if(!s.compile(error))return false;
 }
 if(!enabled){error="Enable at least one stage";return false;}
 activeCount=0;for(size_t i=0;i<stages.size();++i)if(stages[i].enabled)active[activeCount++]=int(i);
 kernel=0;
 auto is=[&](int n,Kind kind){return stages[active[n]].kind==kind;};
 if(activeCount==3&&is(0,Kind::BoxFold)&&is(1,Kind::SphereFold)&&is(2,Kind::Scale))kernel=1;
 else if(activeCount==2&&is(0,Kind::KleinianFold)&&is(1,Kind::Inversion))kernel=4;


 else if(activeCount==3&&is(0,Kind::Tetra)&&is(1,Kind::Scale)&&is(2,Kind::Offset))kernel=3;
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
 const char* names[]={"MANDELBULB / 8","MANDELBULB / 2","MANDELBULB / 3","MANDELBULB / 6","MANDELBULB / 12","JULIA BULB","MANDELBOX","NEGATIVE BOX","CORRIDOR BOX","SPHERE NETWORK","MENGER SPONGE","TWISTED MENGER","SIERPINSKI TETRA","KALEIDO TETRA","BOX-BULB HYBRID","FOLDED JULIA BOX","EXPRESSION JULIA","INVERTED BOX","MANDELBULB / 4","MANDELBULB / 5","ABSOLUTE BULB","ROTATED NEGATIVE BOX","TWISTED JULIA BULB","MENGER / WIDE CUT","RUBY CHAMBERS","BLUE SPHERE VAULT","GOLD BOX CORRIDOR","MENGER COLONNADE","TWISTED BOX HALL","INVERTED BUBBLE HALL","TETRA GALLERY","JULIA BULB ARCADE","BULB GARDEN","ABSOLUTE BULB / 5","BOX-BULB / 4","NEGATIVE BOX / DEEP","KLEINIAN CHAMBERS","KLEINIAN CORRIDOR","KLEINIAN DROPS","KLEINIAN GALLERY"};
 return names[(n%PresetCount+PresetCount)%PresetCount];
}
Camera presetCamera(int n){
 Camera c;
 c.position.z=(n>=6&&n<=9)||n==15||n==17?-9.f:n==14?-7.f:-3.f;
 if(n==9){c.position.z=-14;c.position.y=.5f;}
 if(n==21)c.position.z=-9;
 if(n>=24&&n<=31){c.position={6,0,-3};c.speed=.35f;}
 if(n==24||n==25||n==26||n==27||n==29)c.position={0,.6f,-4.5f};
 if(n==30)c.position={2.5f,0,-3};
 if(n==31||n==32)c.position={1.75f,.3f,-1.75f};
 if(n==35)c.position.z=-4.5f;
 if(n>=36){c.position={0,.25f,-3.2f};c.pitch=-.08f;c.speed=.2f;}
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
 }else if(n==18||n==19){f.logarithmic=true;f.iterations=10;f.stages={stage(Kind::Bulb,n==18?4:5,1,1),stage(Kind::Scale,1,1)};
 }else if(n==20){f.logarithmic=true;f.iterations=10;f.stages={stage(Kind::Absolute),stage(Kind::Bulb,3,1,1),stage(Kind::Scale,1,1)};
 }else if(n==21){f.iterations=14;f.bailout=32;f.stages={stage(Kind::Rotate,.08f,.12f,.03f),stage(Kind::BoxFold,1),stage(Kind::SphereFold,.5f,1),stage(Kind::Scale,-1.5f,1)};
 }else if(n==22){f.iterations=10;f.logarithmic=true;f.julia=true;f.constant={.25f,-.15f,.1f};f.stages={stage(Kind::Rotate,.03f,.1f,.05f),stage(Kind::Bulb,8,1,1),stage(Kind::Scale,1,1)};
 }else if(n==23){f.iterations=6;f.bailout=100;f.terminal=1;f.stages={stage(Kind::Menger,3,1,.7f)};
 }else if(n>=24&&n<=31){
  f.iterations=14;f.bailout=32;f.repeat=true;f.repeatPeriod={12,12,12};
  float scale=n==24?-1.5f:n==25?-1.8f:n==26?2.8f:n==29?-1.8f:2;
  f.stages={stage(Kind::BoxFold,1),stage(Kind::SphereFold,n==25?.32f:n==29?.12f:.5f,1),stage(Kind::Scale,scale,1)};
  if(n==27){f.iterations=7;f.terminal=1;f.bailout=100;f.repeatPeriod={3.2f,3.2f,3.2f};f.stages={stage(Kind::Menger,3,1,1)};}
  if(n==28)f.stages.insert(f.stages.begin(),stage(Kind::Rotate,.03f,.08f,.02f));
  if(n==30){f.iterations=10;f.terminal=2;f.bailout=100;f.repeatPeriod={5,5,5};f.stages={stage(Kind::Tetra),stage(Kind::Scale,2,0),stage(Kind::Offset,-1,-1,-1)};}
  if(n==31){f.julia=true;f.logarithmic=true;f.constant={.25f,-.15f,.1f};f.repeatPeriod={3.5f,3.5f,3.5f};f.stages={stage(Kind::Bulb,8,1,1),stage(Kind::Scale,1,1)};}
 }else if(n==32){f.iterations=12;f.logarithmic=true;f.repeat=true;f.repeatPeriod={3.5f,3.5f,3.5f};f.stages={stage(Kind::Bulb,8,1,1),stage(Kind::Scale,1,1)};
 }else if(n==33){f.iterations=12;f.logarithmic=true;f.stages={stage(Kind::Absolute),stage(Kind::Bulb,5,1,1),stage(Kind::Scale,1,1)};
 }else if(n==34){f.iterations=10;f.logarithmic=true;f.stages={stage(Kind::BoxFold,1),stage(Kind::Bulb,4,1,1),stage(Kind::Scale,1,1)};
 }else if(n==35){f.iterations=20;f.bailout=32;f.stages={stage(Kind::BoxFold,1),stage(Kind::SphereFold,.5f,1),stage(Kind::Scale,-1.5f,1)};
 }
 if(n>=36){
  f.iterations=12;f.bailout=256;f.terminal=3;f.terminalRadius=.8f;
  f.stages={stage(Kind::KleinianFold,.63248f,.78632f,.775f),stage(Kind::Inversion,std::sqrt(.70968f))};
  f.derivativeScale=1.25f;
  if(n==37){f.stages[0].a=.9f;f.stages[0].b=.65f;f.stages[0].c=.9f;f.terminalRadius=.65f;}
  if(n==38){f.stages[0].a=.90756f;f.stages[0].b=.92436f;f.stages[0].c=.90756f;f.stages[1].a=1;f.terminalRadius=.15f;}
  if(n==39){f.stages[0].a=.8f;f.stages[0].b=.8f;f.stages[0].c=1.1f;f.stages.push_back(stage(Kind::Offset,0,0,.12f));f.terminalRadius=.55f;}
 }
 std::string error;f.validate(error);return f;
}
Settings presetSettings(int n){
 Settings s;s.farClip=40;s.convergence=std::fmax(.2f,-presetCamera(n).position.z);
 if(n>=24){
  s.adaptivePrecision=true;s.steps=96;s.fog=.018f;s.specular=.45f;s.customGradient=true;
  s.gradientLow={.08f,.12f,.2f};s.gradientHigh={.65f,.8f,.95f};s.gradientScale=1.2f;
  if(n==24){s.gradientLow={.12f,.008f,.012f};s.gradientHigh={.95f,.035f,.02f};s.specular=.85f;}
  if(n==26||n==27){s.gradientLow={.1f,.07f,.01f};s.gradientHigh={.95f,.95f,.12f};s.gradientRepeat=true;s.gradientScale=3;}
  if(n==28||n==31){s.gradientLow={.12f,.015f,.2f};s.gradientHigh={.85f,.5f,.95f};}
  if(n==32||n==33){s.gradientLow={.02f,.15f,.05f};s.gradientHigh={.7f,.95f,.3f};}
 }
 if(n>=36){s.safety=.45f;s.epsilon=.0005f;s.steps=128;s.farClip=20;s.fog=.025f;s.gradientLow={.025f,.06f,.12f};s.gradientHigh={.65f,.85f,1};if(n==38){s.gradientLow={.1f,.005f,.015f};s.gradientHigh={1,.18f,.08f};}}
 return s;
}
template<bool Trap> Sample evaluateGenericDistance(const Formula& f,Vec p,bool exact=false){
 Vec z=p,c=f.julia?f.constant:p;float dr=1,trap=1e6f,r=0;
 for(int i=0;i<f.iterations;++i){
  if(z.dot(z)>f.bailout*f.bailout)break;
  for(int stageIndex=0;stageIndex<f.activeCount;++stageIndex){const Stage& s=f.stages[f.active[stageIndex]];
   switch(s.kind){
    case Kind::KleinianFold:{z={2*clamp(z.x,-s.a,s.a)-z.x,2*clamp(z.y,-s.b,s.b)-z.y,2*clamp(z.z,-s.c,s.c)-z.z};break;}
    case Kind::Inversion:{float k=std::fmax(1.f,s.minimum2/std::fmax(z.dot(z),1e-12f));z=z*k;dr*=k;break;}
    case Kind::BoxFold:{float limit=std::fabs(s.a);z={2*clamp(z.x,-limit,limit)-z.x,2*clamp(z.y,-limit,limit)-z.y,2*clamp(z.z,-limit,limit)-z.z};break;}
    case Kind::SphereFold:{float r2=z.dot(z),min2=s.minimum2,fixed2=s.fixed2;
     float k=r2<min2?s.innerScale:r2<fixed2?fixed2/std::fmax(r2,1e-12f):1;
     z=z*k;dr*=k;break;
    }
    case Kind::Bulb:{
     r=z.length();if(r<1e-12f){z={};dr=std::fmax(dr,1e-12f);break;}
     float power=radialPower(r,s.a-1,s.integerPower);
     // Angular multipliers can stretch the transform beyond the standard bulb.
     dr*=power*s.stretch;
     float nr=power*r;
     if(f.algebraicBulb&&!exact&&(s.a==2||s.a==4||s.a==8||s.a==16)&&s.b==1&&s.c==1){
      float radial=std::sqrt(z.x*z.x+z.y*z.y),st=radial/r,ct=z.z/r;
      float sp=radial>0?z.y/radial:0,cp=radial>0?z.x/radial:1;
      for(int n=int(s.a);n>1;n>>=1){float ns=2*st*ct,nc=ct*ct-st*st;st=ns;ct=nc;ns=2*sp*cp;nc=cp*cp-sp*sp;sp=ns;cp=nc;}
      z={nr*st*cp,nr*st*sp,nr*ct};
     }else{
      float theta=std::acos(clamp(z.z/r,-1,1))*s.thetaPower,phi=std::atan2(z.y,z.x)*s.phiPower,sinTheta=std::sin(theta);
      z={nr*sinTheta*std::cos(phi),nr*sinTheta*std::sin(phi),nr*std::cos(theta)};
     }break;
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
 }else if(f.terminal==3){
  float xy=std::sqrt(z.x*z.x+z.y*z.y);
  d=std::fmax(xy-f.terminalRadius,std::fabs(xy*z.z)/std::fmax(r,1e-12f))/std::fmax(dr,1e-12f);
 }else if(f.terminal==2){
  d=(std::fmax(-z.x-z.y-z.z,std::fmax(-z.x+z.y+z.z,std::fmax(z.x-z.y+z.z,z.x+z.y-z.z)))-f.terminalRadius)*.57735027f/std::fmax(dr,1e-12f);
 }
 d=std::fmax(0.f,d)/f.derivativeScale;
 return {d,Trap?std::sqrt(trap):0.f,std::isfinite(d)};
}
template<Kind Fixed,bool Trap> inline bool applyStage(const Formula& f,const Stage& s,Vec& z,Vec c,float& dr,float& trap){
   switch(Fixed){
    case Kind::KleinianFold:{z={2*clamp(z.x,-s.a,s.a)-z.x,2*clamp(z.y,-s.b,s.b)-z.y,2*clamp(z.z,-s.c,s.c)-z.z};break;}
    case Kind::Inversion:{float k=std::fmax(1.f,s.minimum2/std::fmax(z.dot(z),1e-12f));z=z*k;dr*=k;break;}
    case Kind::BoxFold:{float limit=std::fabs(s.a);auto fold=[limit](float v){float bounded=v< -limit?-limit:v>limit?limit:v;return 2*bounded-v;};z={fold(z.x),fold(z.y),fold(z.z)};break;}
    case Kind::SphereFold:{float r2=z.dot(z),min2=s.minimum2,fixed2=s.fixed2;
     float k=r2<min2?s.innerScale:r2<fixed2?fixed2/std::fmax(r2,1e-12f):1;
     z=z*k;dr*=k;break;
    }
    case Kind::Scale:z=z*s.a+c*s.b;dr=dr*std::fabs(s.a)+(f.julia?0:std::fabs(s.b));break;
    case Kind::Offset:z=z+Vec{s.a,s.b,s.c};break;
    case Kind::Tetra:
     if(z.x+z.y<0){float t=z.x;z.x=-z.y;z.y=-t;}
     if(z.x+z.z<0){float t=z.x;z.x=-z.z;z.z=-t;}
     if(z.y+z.z<0){float t=z.y;z.y=-z.z;z.z=-t;}break;
    default:return false;
   }
   if(!std::isfinite(z.x)||!std::isfinite(z.y)||!std::isfinite(z.z)||!std::isfinite(dr)||dr>1e30f)return false;
   if constexpr(Trap){float squared=z.dot(z);if(squared<trap)trap=squared;}
 return true;
}
template<bool Trap,int Kernel=0> Sample evaluateDistance(const Formula& f,Vec p){
 Vec z=p,c=f.julia?f.constant:p;float dr=1,trap=1e6f,r=0;
 for(int i=0;i<f.iterations;++i){
  if(z.dot(z)>f.bailout*f.bailout)break;
  if constexpr(Kernel==1){
   if(!applyStage<Kind::BoxFold,Trap>(f,f.stages[f.active[0]],z,c,dr,trap)||!applyStage<Kind::SphereFold,Trap>(f,f.stages[f.active[1]],z,c,dr,trap)||!applyStage<Kind::Scale,Trap>(f,f.stages[f.active[2]],z,c,dr,trap))return {0,trap,false};
  }else if constexpr(Kernel==4){
   if(!applyStage<Kind::KleinianFold,Trap>(f,f.stages[f.active[0]],z,c,dr,trap)||!applyStage<Kind::Inversion,Trap>(f,f.stages[f.active[1]],z,c,dr,trap))return {0,trap,false};
  }else if constexpr(Kernel==3){
   if(!applyStage<Kind::Tetra,Trap>(f,f.stages[f.active[0]],z,c,dr,trap)||!applyStage<Kind::Scale,Trap>(f,f.stages[f.active[1]],z,c,dr,trap)||!applyStage<Kind::Offset,Trap>(f,f.stages[f.active[2]],z,c,dr,trap))return {0,trap,false};
  }else return evaluateGenericDistance<Trap>(f,p);
 }
 r=z.length();float d=f.logarithmic?.5f*std::log(std::fmax(r,1e-12f))*r/std::fmax(dr,1e-12f):r/std::fmax(dr,1e-12f);
 if(f.terminal==1){Vec q{std::fabs(z.x)-f.terminalRadius,std::fabs(z.y)-f.terminalRadius,std::fabs(z.z)-f.terminalRadius};
  Vec outside{std::fmax(q.x,0.f),std::fmax(q.y,0.f),std::fmax(q.z,0.f)};
  d=(outside.length()+std::fmin(std::fmax(q.x,std::fmax(q.y,q.z)),0.f))/std::fmax(dr,1e-12f);
 }else if(f.terminal==3){
  float xy=std::sqrt(z.x*z.x+z.y*z.y);
  d=std::fmax(xy-f.terminalRadius,std::fabs(xy*z.z)/std::fmax(r,1e-12f))/std::fmax(dr,1e-12f);
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
template<bool Trap> Sample dispatch(const Formula& f,Vec p,bool exact=false){
 if(f.kernel==1)return evaluateDistance<Trap,1>(f,p);
 if(f.kernel==4)return evaluateDistance<Trap,4>(f,p);
 if(f.kernel==3)return evaluateDistance<Trap,3>(f,p);
 return evaluateGenericDistance<Trap>(f,p,exact);
}
Sample distance(const Formula& f,Vec p,bool exact){return dispatch<true>(f,repeatPoint(f,p),exact);}
Sample distanceOnly(const Formula& f,Vec p,bool exact){return dispatch<false>(f,repeatPoint(f,p),exact);}
Sample distanceGeneric(const Formula& f,Vec p,bool trap){p=repeatPoint(f,p);return trap?evaluateGenericDistance<true>(f,p,true):evaluateGenericDistance<false>(f,p,true);}
}
