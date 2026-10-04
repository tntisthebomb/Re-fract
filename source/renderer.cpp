#include "engine.hpp"
#include <algorithm>

namespace rf {
Rays::Rays(const Scene& s){
 float sy=std::sin(s.camera.yaw),cy=std::cos(s.camera.yaw),sp=std::sin(s.camera.pitch),cp=std::cos(s.camera.pitch);
 forward={sy*cp,sp,cy*cp};right={cy,0,-sy};up={-sy*sp,cp,-cy*sp};
 tangent=std::tan(s.settings.fov*3.14159265f/360);
 light={std::sin(s.settings.lightYaw)*std::cos(s.settings.lightPitch),std::sin(s.settings.lightPitch),-std::cos(s.settings.lightYaw)*std::cos(s.settings.lightPitch)};
}
void Rays::ray(const Scene& s,float x,float y,float eye,Vec& origin,Vec& direction)const{
 origin=s.camera.position+right*eye;
 float px=(2*x/W-1)*tangent*(float(W)/H)-eye/s.settings.convergence;
 float py=(1-2*y/H)*tangent;
 direction=(forward+right*px+up*py).unit();
}
namespace {
Color quantize(Vec v){return {uint8_t(clamp(v.x,0,1)*255),uint8_t(clamp(v.y,0,1)*255),uint8_t(clamp(v.z,0,1)*255)};}
Vec palette(float trap,float select){
 float t=clamp(trap*.8f,0,1);int n=int(select)%4;
 if(n==1)return Vec{.13f,.4f,.48f}*(1-t)+Vec{.72f,.85f,.55f}*t;
 if(n==2)return Vec{.3f,.09f,.36f}*(1-t)+Vec{.95f,.43f,.63f}*t;
 if(n==3)return Vec{.18f,.19f,.17f}*(1-t)+Vec{.85f,.86f,.73f}*t;
 return Vec{.18f,.06f,.025f}*(1-t)+Vec{.95f,.58f,.22f}*t;
}
}
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye){
 Vec origin,dir;rays.ray(s,x,y,eye,origin,dir);
 float t=0,trap=0;bool hit=false;const Settings& o=s.settings;
 int steps=o.quality?std::min(512,o.steps*2):o.steps;
 for(int i=0;i<steps&&t<o.farClip;++i){
  Sample d=distance(s.formula,origin+dir*t);
  if(!d.valid)return {180,20,90};
  float eps=o.epsilon*(o.quality?.5f:1.f)*std::fmax(1.f,t*.2f);
  if(d.distance<eps){hit=true;trap=d.trap;break;}
  t+=std::fmax(d.distance*o.safety,eps*.25f);
 }
 Vec background{.025f,.028f,.024f};
 if(!hit)return quantize(background+Vec{.018f,.012f,.006f}*(1-y/H));
 Vec p=origin+dir*t;float e=std::fmax(o.epsilon,t*o.epsilon*.25f);
 // Tetrahedral normal: four DE evaluations instead of six central differences.
 Vec v1{1,-1,-1},v2{-1,-1,1},v3{-1,1,-1},v4{1,1,1};
 Vec n=(v1*distance(s.formula,p+v1*e).distance+v2*distance(s.formula,p+v2*e).distance+
        v3*distance(s.formula,p+v3*e).distance+v4*distance(s.formula,p+v4*e).distance).unit();
 float diffuse=std::fmax(0.f,n.dot(rays.light)),ao=1,shadow=1;
 for(int i=1;i<=o.ao;++i){float h=e*6*i;Sample d=distance(s.formula,p+n*h);ao-=std::fmax(0.f,h-d.distance)/h*(.35f/i);}
 if(o.shadow){float st=e*8;Vec start=p+n*(e*4);
  for(int i=0;i<o.shadow&&st<8;++i){Sample d=distance(s.formula,start+rays.light*st);
   if(!d.valid||d.distance<e){shadow=.15f;break;}
   shadow=std::fmin(shadow,12*d.distance/st);st+=std::fmax(d.distance*o.safety,e);
  }
 }
 float spec=std::pow(std::fmax(0.f,n.dot((rays.light-dir).unit())),24)*o.specular*shadow;
 Vec color=palette(trap,o.palette)*(clamp(ao,.15f,1)*(.22f+.78f*diffuse*clamp(shadow,.15f,1)))+Vec{spec,spec,spec};
 float fog=std::exp(-t*o.fog);color=(color*fog+background*(1-fog))*o.exposure;
 return quantize(color);
}
Renderer::Renderer(){for(auto& p:pixels)p.resize(W*H);}
Color shadeCell(const Scene& s,const Rays& rays,int x,int y,int block,float eye,int count){
 int red=0,green=0,blue=0;
 for(int sample=0;sample<count;++sample){
  float jx=count==1?.5f:(sample&1)?.75f:.25f,jy=count<=2?.5f:(sample&2)?.75f:.25f;
  Color c=trace(s,rays,std::min(float(W)-.5f,x+block*jx),std::min(float(H)-.5f,y+block*jy),eye);
  red+=c.r;green+=c.g;blue+=c.b;
 }
 return {uint8_t(red/count),uint8_t(green/count),uint8_t(blue/count)};
}
void Renderer::invalidate(const Scene& s,bool motion){
 // Keep the coarse scan position across camera motion: every region eventually refreshes.
 // A new still frame always starts from a complete coarse pass, then refines.
 if(!(motion&&moving)){index=0;lane=0;}
 moving=motion;block=motion?s.settings.previewBlock:(s.settings.quality?1:s.settings.previewBlock);
 done=false;
}
void Renderer::step(const Scene& s,const Rays& rays,float slider){
 if(done)return;
 const Settings& o=s.settings;int nx=(W+block-1)/block,ny=(H+block-1)/block,total=nx*ny;
 int lanes=std::min(o.interlace,total);
 int cell=lane+index*lanes;
 if(cell>=total){++lane;index=0;if(lane>=lanes){lane=0;
   if(moving){return;}else if(block>1&&o.autoRefine){block=std::max(1,block/2);return;}else {done=true;return;}
  }return;
 }
 int x=(cell%nx)*block,y=(cell/nx)*block;++index;
 bool stereo=o.stereo&&slider>.001f;
 int eyes=stereo?2:1,count=o.quality?o.samples:1;
 Color colors[2];
 if(eyes==2&&stereoShader&&o.parallel&&block<=4){
  stereoShader(s,rays,x,y,block,o.eyeSeparation*slider*.5f,count,colors[0],colors[1],shaderContext);
 }else for(int eye=0;eye<eyes;++eye){
  float offset=stereo?(eye?1.f:-1.f)*o.eyeSeparation*slider*.5f:0;
  colors[eye]=shadeCell(s,rays,x,y,block,offset,count);
 }
 rayCount+=eyes*count;
 for(int eye=0;eye<eyes;++eye){
  Color color=colors[eye];
  for(int yy=y;yy<std::min(y+block,H);++yy)for(int xx=x;xx<std::min(x+block,W);++xx)pixels[eye][yy*W+xx]=color;
 }
 if(!stereo)for(int yy=y;yy<std::min(y+block,H);++yy)for(int xx=x;xx<std::min(x+block,W);++xx)pixels[1][yy*W+xx]=pixels[0][yy*W+xx];
}
float Renderer::progress(const Scene& s)const{
 if(done)return 1;
 int total=((W+block-1)/block)*((H+block-1)/block),lanes=std::min(s.settings.interlace,total);
 return clamp(float(lane)/lanes+float(index)/total,0,1);
}
}
