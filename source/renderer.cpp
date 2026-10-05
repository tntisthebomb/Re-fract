#include "engine.hpp"
#include "distance_field.hpp"
#include <algorithm>

namespace rf {
Rays::Rays(const Scene& s){
 float sy=std::sin(s.camera.yaw),cy=std::cos(s.camera.yaw),sp=std::sin(s.camera.pitch),cp=std::cos(s.camera.pitch);
 forward={sy*cp,sp,cy*cp};right={cy,0,-sy};up={-sy*sp,cp,-cy*sp};
 tangent=std::tan(s.settings.fov*3.14159265f/360);
 light={std::sin(s.settings.lightYaw)*std::cos(s.settings.lightPitch),std::sin(s.settings.lightPitch),-std::cos(s.settings.lightYaw)*std::cos(s.settings.lightPitch)};
 for(int i=0;i<2;++i){const auto& p=s.settings.pointLights[i];
  pointPositions[i]=p.cameraRelative?s.camera.position+right*p.position.x+up*p.position.y+forward*p.position.z:p.position;
  if(p.enabled&&p.intensity>0)pointMask|=1<<i;
 }
}
void Rays::ray(const Scene& s,float x,float y,float eye,Vec& origin,Vec& direction)const{
 origin=s.camera.position+right*eye;
 float px=(2*x/W-1)*tangent*(float(W)/H)-eye/s.settings.convergence;
 float py=(1-2*y/H)*tangent;
 direction=(forward+right*px+up*py).unit();
}
void Rays::lensRay(const Scene& s,float x,float y,float eye,float lensX,float lensY,Vec& origin,Vec& direction)const{
 ray(s,x,y,eye,origin,direction);
 if(s.settings.aperture<=0)return;
 Vec focus=origin+direction*(s.settings.focusDistance/std::fmax(1e-6f,direction.dot(forward)));
 origin=origin+(right*lensX+up*lensY)*s.settings.aperture;
 direction=(focus-origin).unit();
}
namespace {
Color quantize(Vec v){return {uint8_t(clamp(v.x,0,1)*255),uint8_t(clamp(v.y,0,1)*255),uint8_t(clamp(v.z,0,1)*255)};}
float radicalInverse(uint32_t n,uint32_t base){float value=0,weight=1.f/base;while(n){value+=(n%base)*weight;n/=base;weight/=base;}return value;}
int lightingTarget(const Settings& s){return s.quality?1:std::max(s.stillBlock,s.lightingBlock);}
bool progressiveActive(const Settings& s){return s.progressiveLighting&&((s.giSamples>0&&s.giStrength>0)||(s.dof&&s.aperture>0));}
}
float hitTolerance(const Settings& o,float t,float tangent){
 float ceiling=o.epsilon*(o.quality?.5f:1.f)*std::fmax(1.f,t*.2f);
 if(!o.adaptivePrecision)return ceiling;
 // World-space footprint of a pixel, bounded to avoid precision below float resolution.
 return std::fmin(ceiling,std::fmax(o.minEpsilon,2*t*tangent/H*o.pixelTolerance));
}
int renderingIterations(const Scene& s,bool moving){
 if(!s.settings.adaptiveDetail)return s.formula.iterations;
 if(moving)return std::min(s.formula.iterations,s.settings.previewIterations);
 Sample proximity=distanceOnly(s.formula,s.camera.position);
 if(!proximity.valid||proximity.distance>=.25f)return s.formula.iterations;
 float ratio=.25f/std::fmax(proximity.distance,s.settings.minEpsilon);
 int extra=std::max(0,int(std::ceil(std::log2(ratio))));
 return std::min(std::max(s.formula.iterations,s.settings.detailIterations),s.formula.iterations+extra);
}
Color recolorSample(const Settings& o,const ShadeRecord& r){
 if(r.background)return quantize(o.skyColor*o.exposure);
 Vec background=o.skyColor;float fog=std::exp(-r.depth*o.fog);
 Vec albedo=gradientColor(o,r.trap);
 Vec color=albedo*(r.lighting+o.emission)+albedo.multiply(r.indirect)+Vec{r.specular,r.specular,r.specular};
 color=color+albedo.multiply(r.localDiffuse)+r.localSpecular;
 return quantize((color*fog+background*(1-fog))*o.exposure);
}
static RenderResult sampleRay(const Scene& s,const Rays& rays,float x,float y,float eye,bool fast,uint32_t sequence=0,bool varied=false,bool lens=false,bool indirect=true){
 RenderResult result;result.profile.rays=1;
 // Per-ray counts fit in 32 bits; publish 64-bit totals once, avoiding paired
 // loads/stores on ARM11 for every distance-estimator call.
 uint32_t queries=0,shadingQueries=0,stepsTaken=0;
 const DistanceField* field=fast&&!s.settings.quality?rays.field:nullptr;
 auto query=[&](Vec p,bool shading=false){float cached;if(field&&!shading&&field->lookup(p,cached)){++result.profile.reused;return Sample{cached,0,true};}++queries;if(shading)++shadingQueries;return distanceOnly(s.formula,p,s.settings.quality);};
 auto finish=[&](Color c){result.color=c;result.radiance=Vec{float(c.r),float(c.g),float(c.b)}*(1.f/255);result.profile.distanceQueries=queries;result.profile.shadingQueries=shadingQueries;result.profile.steps=stepsTaken;return result;};
 auto finishLinear=[&](Vec color){auto out=finish(quantize(color));out.radiance=color;return out;};
 Vec origin,dir;
 if(lens){float radius=std::sqrt(radicalInverse(sequence+1,2)),angle=6.2831853f*radicalInverse(sequence+1,3);
  rays.lensRay(s,x,y,eye,radius*std::cos(angle),radius*std::sin(angle),origin,dir);
 }else rays.ray(s,x,y,eye,origin,dir);
 float t=0,trap=0;bool hit=false;const Settings& o=s.settings;
 float previousT=0,previousD=0,advance=0;bool relaxed=false;
 int steps=o.quality?std::min(8192,o.steps*2):o.steps;
 for(int i=0;i<steps&&t<o.farClip;++i){
  ++stepsTaken;Sample d=query(origin+dir*t);
  if(!d.valid)return finish({180,20,90});
  if(relaxed&&previousD+d.distance<advance){t=previousT+std::fmax(previousD*o.safety,o.epsilon*.25f);d=query(origin+dir*t);++result.profile.relaxFallbacks;if(!d.valid)return finish({180,20,90});relaxed=false;}
  float eps=hitTolerance(o,t,rays.tangent);
  if(d.distance<eps){hit=true;trap=d.trap;break;}
  previousT=t;previousD=d.distance;advance=std::fmax(d.distance*o.safety,eps*.25f);
  relaxed=!o.quality&&!s.formula.repeat&&o.relaxation>1; if(relaxed)advance*=o.relaxation;
  // Stop at a periodic-cell boundary before tracing the next cell's geometry.
  if(s.formula.repeat)advance=std::fmin(advance,repeatBoundaryStep(s.formula,origin+dir*t,dir)+eps*.25f);
  t+=advance;
 }
 Vec background=o.skyColor;
 if(!hit){result.shade.valid=true;result.shade.background=true;result.shade.escaped=t>=o.farClip;return finishLinear(background*o.exposure);}
 Vec p=origin+dir*t;result.hit=true;result.point=p;result.depth=t;
 ++queries;++shadingQueries;trap=distance(s.formula,p,o.quality).trap;
 if(fast){float fog=std::exp(-t*o.fog);return finishLinear((gradientColor(o,trap)*((1+o.emission)*fog)+background*(1-fog))*o.exposure);}
 float e=o.adaptivePrecision?hitTolerance(o,t,rays.tangent):std::fmax(o.epsilon,t*o.epsilon*.25f);
 // At large coordinates, a sub-ULP normal offset would sample the same point.
 if(o.adaptivePrecision)e=std::fmax(e,4*1.1920929e-7f*std::fmax(1.f,std::fmax(std::fabs(p.x),std::fmax(std::fabs(p.y),std::fabs(p.z)))));
 // Tetrahedral normal: four DE evaluations instead of six central differences.
 Vec v1{1,-1,-1},v2{-1,-1,1},v3{-1,1,-1},v4{1,1,1};
 Vec n=(v1*query(p+v1*e,true).distance+v2*query(p+v2*e,true).distance+
        v3*query(p+v3*e,true).distance+v4*query(p+v4*e,true).distance).unit();
 float diffuse=std::fmax(0.f,n.dot(rays.light)),ao=1,shadow=1;
 float baseSpec=o.specular>0&&o.sunStrength>0?std::pow(std::fmax(0.f,n.dot((rays.light-dir).unit())),24)*o.specular:0;
 for(int i=1;i<=o.ao&&ao>.15f;++i){float h=e*6*i;Sample d=query(p+n*h,true);ao-=std::fmax(0.f,h-d.distance)/h*(.35f/i);}
 if(o.sunStrength>0&&o.shadow&&(diffuse>0||baseSpec>0)){float st=e*8;Vec start=p+n*(e*4);
  for(int i=0;i<o.shadow&&st<8;++i){Sample d=query(start+rays.light*st,true);
   if(!d.valid||d.distance<e){shadow=.15f;break;}
   shadow=std::fmin(shadow,12*d.distance/st);st+=std::fmax(d.distance*o.safety,e);
  }
 }
 auto pointLighting=[&](Vec position,Vec normal,float offset,Vec view,bool highlights,Vec& illumination,Vec& gloss){
  for(int i=0;i<2;++i){if(!(rays.pointMask&(1<<i)))continue;const auto& lamp=o.pointLights[i];
   Vec delta=rays.pointPositions[i]-position;float length=delta.length();if(length>=lamp.range||length<=offset)continue;
   Vec direction=delta*(1/length);float lambert=std::fmax(0.f,normal.dot(direction));
   float specular=highlights&&o.specular>0?std::pow(std::fmax(0.f,normal.dot((direction-view).unit())),24)*o.specular:0;
   if(lambert==0&&specular==0)continue;
   float visibility=1;
   if(o.pointShadows&&o.shadow>0){float travel=offset*8;Vec start=position+normal*(offset*4);
    for(int k=0;k<o.shadow&&travel<length;++k){Vec q=start+direction*travel;auto d=query(q,true);
     if(!d.valid||d.distance<offset){visibility=0;break;}
     visibility=std::fmin(visibility,12*d.distance/travel);
     float stride=std::fmax(d.distance*o.safety,offset);
     if(s.formula.repeat)stride=std::fmin(stride,repeatBoundaryStep(s.formula,q,direction)+offset*.25f);
     travel+=stride;
    }
    if(travel<length)visibility=0;
   }
   float falloff=1-length/lamp.range;float power=lamp.intensity*falloff*falloff/(1+length*length)*visibility;
   illumination=illumination+lamp.color*(lambert*power);gloss=gloss+lamp.color*(specular*power);
  }
 };
 float spec=baseSpec*shadow*o.sunStrength;
 result.shade={trap,t,clamp(ao,.15f,1)*(.22f+.78f*diffuse*clamp(shadow,.15f,1)*o.sunStrength),spec,true};
 if(rays.pointMask)pointLighting(p,n,e,dir,true,result.shade.localDiffuse,result.shade.localSpecular);
 if(indirect&&o.giSamples>0&&o.giStrength>0){
  // Deterministic cosine-weighted hemisphere samples: no recursion, RNG or frame flicker.
  Vec axis=std::fabs(n.z)<.9f?Vec{0,0,1}:Vec{0,1,0};
  auto cross=[](Vec a,Vec b){return Vec{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
  Vec u=cross(axis,n).unit(),v=cross(n,u);Vec incoming;
  const float offsets[4][2]={{.35f,.35f},{-.65f,.25f},{.25f,-.65f},{-.45f,-.45f}};
  for(int j=0;j<o.giSamples;++j){
   float a=offsets[j%4][0],b=offsets[j%4][1];
   if(varied||o.giSamples>4){uint32_t index=sequence*uint32_t(o.giSamples)+j+1;
    uint32_t hash=uint32_t(x)*1973u+uint32_t(y)*9277u;
    float radius=std::sqrt(radicalInverse(index,2)),angle=6.2831853f*std::fmod(radicalInverse(index,3)+(hash&65535u)/65536.f,1.f);
    a=radius*std::cos(angle);b=radius*std::sin(angle);
   }
   Vec direction=u*a+v*b+n*std::sqrt(std::fmax(0.f,1-a*a-b*b));
   Vec start=p+n*(e*4);float travel=e*4;bool bounced=false,valid=true;
   for(int k=0;k<o.giSteps&&travel<o.giRange;++k){
    Vec q=start+direction*travel;Sample d=query(q);if(!d.valid){valid=false;break;}
    if(d.distance<e){
     // Approximate one diffuse bounce, with an estimated secondary normal.
     Vec bn=(v1*query(q+v1*e,true).distance+v2*query(q+v2*e,true).distance+
              v3*query(q+v3*e,true).distance+v4*query(q+v4*e,true).distance).unit();
     ++queries;++shadingQueries;Sample material=distance(s.formula,q,o.quality);
     if(material.valid){Vec bounce=gradientColor(o,material.trap);Vec local,unused;
      if(rays.pointMask)pointLighting(q,bn,e,direction,false,local,unused);
      incoming=incoming+bounce*(.15f+.85f*std::fmax(0.f,bn.dot(rays.light))*o.sunStrength+o.emission)+bounce.multiply(local);
     }
     bounced=true;break;
    }
    float stride=std::fmax(d.distance*o.safety,e);
    if(s.formula.repeat)stride=std::fmin(stride,repeatBoundaryStep(s.formula,q,direction)+e*.25f);
    travel+=stride;
   }
   // Exhausted rays are treated as occluded, rather than leaking skylight.
   if(valid&&!bounced&&travel>=o.giRange)incoming=incoming+o.skyColor*(.5f+.5f*std::fmax(0.f,direction.y));
  }
  result.shade.indirect=incoming*(o.giStrength/o.giSamples);
 }
 Vec albedo=gradientColor(o,trap);
 Vec color=albedo*(result.shade.lighting+o.emission)+Vec{spec,spec,spec};
 if(rays.pointMask)color=color+albedo.multiply(result.shade.localDiffuse)+result.shade.localSpecular;
 if(o.giSamples>0&&o.giStrength>0)color=color+albedo.multiply(result.shade.indirect);
 float fog=std::exp(-t*o.fog);color=(color*fog+background*(1-fog))*o.exposure;
 return finishLinear(color);
}
bool StereoSlider::update(const Settings& s,float raw){
 float previous=strength;raw=std::isfinite(raw)?clamp(raw,0,1):0;
 if(!s.stereo||s.eyeSeparation<=0){active=false;strength=0;}
 else{if(raw<=.02f)active=false;else if(raw>=.04f)active=true;
  if(!active)strength=0;
  else if(strength==0||std::fabs(raw-strength)>.02f)strength=std::round(raw*32)/32;
 }
 return strength!=previous;
}
float cameraSpeedScale(const Scene& s){
 if(!s.camera.surfaceSpeed)return 1;
 Sample d=distanceOnly(s.formula,s.camera.position);
 if(!d.valid||!std::isfinite(d.distance))return s.camera.minimumSpeed;
 return clamp(d.distance/s.camera.surfaceRange,s.camera.minimumSpeed,1);
}
Vec gradientColor(const Settings& s,float trap){
 float input=s.boundedGradient?std::fmax(trap,0.f)/(1+std::fmax(trap,0.f)):trap;
 float t=input*s.gradientScale+s.gradientOffset;
 t=s.gradientRepeat?t-std::floor(t):clamp(t,0,1);
 Vec lo=s.gradientLow,hi=s.gradientHigh;
 if(!s.customGradient){int n=int(s.palette)%4;
  if(n==1){lo={.13f,.4f,.48f};hi={.72f,.85f,.55f};}
  else if(n==2){lo={.3f,.09f,.36f};hi={.95f,.43f,.63f};}
  else if(n==3){lo={.18f,.19f,.17f};hi={.85f,.86f,.73f};}
  else{lo={.18f,.06f,.025f};hi={.95f,.58f,.22f};}
 }
 if(s.customGradient&&s.gradientStops>2){
  int count=std::min(5,s.gradientStops);float u=t*(count-1);int index=std::min(count-2,int(u));float mix=u-index;
  auto stop=[&](int i){return i==0?lo:i==count-1?hi:s.gradientMiddle[i-1];};
  return stop(index)*(1-mix)+stop(index+1)*mix;
 }
 return lo*(1-t)+hi*t;
}
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye){return sampleRay(s,rays,x,y,eye,false).color;}
void Profile::add(const Profile& p){rays+=p.rays;steps+=p.steps;distanceQueries+=p.distanceQueries;shadingQueries+=p.shadingQueries;reused+=p.reused;skipped+=p.skipped;relaxFallbacks+=p.relaxFallbacks;batches+=p.batches;skySkipped+=p.skySkipped;}
RenderResult renderJob(const Scene& s,const Rays& rays,const RenderJob& j){
 RenderResult result;int red=0,green=0,blue=0,count=s.settings.quality?s.settings.samples:1;
 bool coarseLighting=!j.moving&&progressiveActive(s.settings)&&s.settings.autoRefine&&j.block>lightingTarget(s.settings);
 bool lens=s.settings.dof&&s.settings.aperture>0&&!j.moving&&!coarseLighting;
 if(lens)count=std::max(count,s.settings.dofSamples);
 Vec radiance;
 for(int i=0;i<count;++i){float jx=count==1?.5f:(i&1)?.75f:.25f,jy=count<=2?.5f:(i&2)?.75f:.25f;
  // Lens samples repeat the same 1/2/4 pixel pattern while varying the aperture.
  auto r=sampleRay(s,rays,std::min(W-.5f,j.x+j.block*jx),std::min(H-.5f,j.y+j.block*jy),j.offset,j.fast&&!s.settings.quality,
   j.sample*uint32_t(count)+i,j.accumulate||lens,lens,!coarseLighting);
  if(i==0){result.shade=r.shade;result.point=r.point;result.depth=r.depth;result.hit=r.hit;}
  result.profile.add(r.profile);red+=r.color.r;green+=r.color.g;blue+=r.color.b;radiance=radiance+r.radiance;
 }
 if(count!=1||lens||j.accumulate)result.shade.valid=false;
 result.radiance=radiance*(1.f/count);
 result.color=lens||j.accumulate?quantize(result.radiance):Color{uint8_t(red/count),uint8_t(green/count),uint8_t(blue/count)};
 return result;
}
void renderJobs(const Scene& s,const Rays& r,const RenderJob* jobs,RenderResult* out,int n){for(int i=0;i<n;++i)out[i]=renderJob(s,r,jobs[i]);}
Renderer::Renderer(){for(int e=0;e<2;++e){pixels[e].resize(W*H);depths[e].resize(W*H);ages[e].resize(W*H);shades[e].resize(W*H);}}
Color shadeCell(const Scene& s,const Rays& r,int x,int y,int b,float eye,int count){
 int red=0,green=0,blue=0;for(int i=0;i<count;++i){float jx=count==1?.5f:(i&1)?.75f:.25f,jy=count<=2?.5f:(i&2)?.75f:.25f;
 Color c=trace(s,r,std::min(W-.5f,x+b*jx),std::min(H-.5f,y+b*jy),eye);red+=c.r;green+=c.g;blue+=c.b;}
 return {uint8_t(red/count),uint8_t(green/count),uint8_t(blue/count)};
}
bool Rays::project(const Scene& s,Vec p,float eye,float& x,float& y,float& depth)const{
 Vec v=p-(s.camera.position+right*eye);float z=v.dot(forward);if(z<=.001f)return false;
 x=W*.5f*(1+(v.dot(right)/z+eye/s.settings.convergence)/(tangent*float(W)/H));
 y=H*.5f*(1-v.dot(up)/z/tangent);depth=v.length();return std::isfinite(x)&&std::isfinite(y)&&x>=0&&x<W&&y>=0&&y<H&&depth<s.settings.farClip;
}
void Renderer::invalidate(const Scene& s,bool motion,bool clearHistory){
 cacheReady=false;lightingPass=finishedLightingPasses=0;
 if(clearHistory)presentedReady=false;
 if(clearHistory||motion||!s.settings.adaptiveEmpty){emptyBlock=0;emptyPass=0;}
 if(!progressiveActive(s.settings))for(auto& sum:accumulation)std::vector<Vec>().swap(sum);
 int next=motion?(s.settings.adaptiveResolution?dynamicBlock:s.settings.previewBlock):(s.settings.quality?1:std::max(s.settings.previewBlock,s.settings.stillBlock));
 if(clearHistory||!(motion&&moving)||next!=block){index=0;lane=0;passTiming=false;}
 moving=motion;block=next;done=false;
 if(clearHistory){historyCount=historyCursor=0;for(auto& a:ages)std::fill(a.begin(),a.end(),0);}
}
void Renderer::reproject(const Scene& s,const Rays& r,float slider){
 ++revision;
 for(int e=0;e<2;++e){std::fill(pixels[e].begin(),pixels[e].end(),Color{6,7,6});std::fill(depths[e].begin(),depths[e].end(),0);std::fill(ages[e].begin(),ages[e].end(),0);}
 int eyes=s.settings.stereo&&slider>.001f&&s.settings.eyeSeparation>0?2:1;
 for(size_t i=0;i<historyCount;++i){const auto& h=history[i];if(frame-h.stamp>uint32_t(s.settings.historyFrames))continue;
  for(int e=0;e<eyes;++e){if(e!=h.eye&&!s.settings.stereoReuse)continue;float x,y,d,offset=eyes==2?(e?1.f:-1.f)*s.settings.eyeSeparation*slider*.5f:0;
   if(!r.project(s,h.point,offset,x,y,d))continue;
   int radius=int(clamp(h.footprint/d*H/(2*r.tangent),1,16));
   for(int yy=std::max(0,int(y)-radius);yy<std::min(H,int(y)+radius+1);++yy)for(int xx=std::max(0,int(x)-radius);xx<std::min(W,int(x)+radius+1);++xx){int k=yy*W+xx;
    if(depths[e][k]==0||d<depths[e][k]){depths[e][k]=d;pixels[e][k]=h.color;ages[e][k]=1;}
   }
  }
 }

}
void Renderer::beginFrame(const Scene& s,bool motion,bool changed,float slider){
 ++frame;int eyes=s.settings.stereo&&slider>.001f&&s.settings.eyeSeparation>0?2:1;
 float offset=eyes==2?s.settings.eyeSeparation*slider*.5f:0;
 bool eyesChanged=eyes!=activeEyes||offset!=eyeOffset;
 if(eyesChanged){historyCount=historyCursor=0;for(auto& a:ages)std::fill(a.begin(),a.end(),0);}
 activeEyes=eyes;eyeOffset=offset;
 if(changed||eyesChanged){dynamicBlock=s.settings.previewBlock;invalidate(s,motion,true);}
 else if(motion||moving)invalidate(s,motion,false);
 if(motion&&s.settings.temporal&&!s.settings.quality)reproject(s,Rays(s),slider);
}
bool Renderer::recolor(const Scene& s){
 if(!cacheReady||!done||moving||s.settings.bloom>0||s.settings.giSamples>0||(s.settings.dof&&s.settings.aperture>0)||progressiveActive(s.settings))return false;
 // The final pass shades one ray per block; avoid repeating exp/gradient math
 // for every replicated pixel in a completed coarse render.
 struct Work{Renderer* renderer;const Scene* scene;int rows;};Work work{this,&s,(H+block-1)/block};
 auto row=[](int i,void* context){auto& work=*static_cast<Work*>(context);auto& self=*work.renderer;
  int e=i/work.rows,y=(i%work.rows)*self.block;
  for(int x=0;x<W;x+=self.block){const auto& record=self.shades[e][y*W+x];if(!record.valid)continue;
   Color color=recolorSample(work.scene->settings,record);
   for(int yy=y;yy<std::min(y+self.block,H);++yy)std::fill(self.pixels[e].begin()+yy*W+x,self.pixels[e].begin()+yy*W+std::min(x+self.block,W),color);
  }
 };
 int rows=activeEyes*work.rows;
 if(parallelFor&&s.settings.parallel&&block<=4)parallelFor(rows,row,&work,loopContext);
 else for(int i=0;i<rows;++i)row(i,&work);
 applyBloom(s.settings,activeEyes);present(s.settings,activeEyes);historyCount=historyCursor=0;++revision;return true;
}
bool Renderer::captureSurface(const Scene& s,SurfaceMesh& mesh)const{
 if(!s.settings.gpuCache||!cacheReady||!done||moving||s.settings.bloom>0||block>s.settings.meshStride||(s.settings.dof&&s.settings.aperture>0)||progressiveActive(s.settings))return false;
 SurfaceMesh next=surfaceMesh(s,pixels[0],depths[0],block,activeEyes==2?-eyeOffset:0,parallelFor,loopContext);
 if(next.indices.empty())return false;
 mesh=std::move(next);return true;
}
void Renderer::endFrame(const Scene& s,float totalMs,float renderMs,uint64_t jobs){
 frameMs=totalMs;if(jobs){int samples=s.settings.quality?s.settings.samples:1;if(!moving&&s.settings.dof&&s.settings.aperture>0)samples=std::max(samples,s.settings.dofSamples);float cost=renderMs*samples/jobs;averageJobMs=averageJobMs==0?cost:averageJobMs*.8f+cost*.2f;}
 batchLimit=std::max(1,std::min(s.settings.batchSize,int(s.settings.budgetMs*.5f/std::max(.001f,averageJobMs))));
 if(moving&&s.settings.adaptiveResolution&&frame%15==0){float target=1000.f/s.settings.targetFps;
 float estimate=averageJobMs*((W+dynamicBlock-1)/dynamicBlock)*((H+dynamicBlock-1)/dynamicBlock)*activeEyes;
 if(estimate>target*2)dynamicBlock=std::min(32,dynamicBlock*2);
 else if(estimate*4<target*1.4f)dynamicBlock=std::max(2,dynamicBlock/2);}
}
bool Renderer::skipCell(const Scene& s,int x,int y,int eye,int cell){
 if(!moving&&!s.settings.quality&&s.settings.adaptiveEmpty&&emptyBlock>0&&(s.settings.emptyRefresh==0||emptyPass%s.settings.emptyRefresh!=0)){
  int mx=std::min(emptyColumns-1,x/emptyBlock),my=std::min(emptyRows-1,y/emptyBlock);
  unsigned hash=unsigned(x/block)*13u+unsigned(y/block)*7u+unsigned(eye)*3u+unsigned(lightingPass)*5u;
  unsigned mask=emptyMask[eye][my*emptyColumns+mx],frequency=mask==2?unsigned(s.settings.skyEdgeProbe):unsigned(s.settings.emptyProbe);
  if(mask&&hash%frequency!=0){
   ShadeRecord sky;sky.valid=true;sky.background=sky.escaped=true;shades[eye][y*W+x]=sky;
   if(progressiveActive(s.settings)&&(block<=lightingTarget(s.settings)||!s.settings.autoRefine))accumulation[eye][y*W+x]=s.settings.skyColor*(s.settings.exposure*(lightingPass+1));
   Color color=recolorSample(s.settings,sky);
   for(int yy=y;yy<std::min(H,y+block);++yy){int first=yy*W+x,last=yy*W+std::min(W,x+block);std::fill(pixels[eye].begin()+first,pixels[eye].begin()+last,color);std::fill(depths[eye].begin()+first,depths[eye].begin()+last,0);}
   ++totals.skipped;++totals.skySkipped;return true;
  }
 }

 if(s.settings.quality||s.settings.bloom>0||(!moving&&((s.settings.dof&&s.settings.aperture>0)||progressiveActive(s.settings))))return false;
 if(!moving&&s.settings.adaptiveTiles&&block<4){
  int cx=std::min(W-1,x+block/2),cy=std::min(H-1,y+block/2);float center=depths[eye][cy*W+cx];Color c=pixels[eye][cy*W+cx];bool flat=center>0;
  for(int dy:{-4,4})for(int dx:{-4,4}){int k=std::max(0,std::min(H-1,cy+dy))*W+std::max(0,std::min(W-1,cx+dx));Color q=pixels[eye][k];
   flat=flat&&depths[eye][k]>0&&std::fabs(depths[eye][k]-center)<center*.005f&&std::abs(int(c.r)-q.r)<4&&std::abs(int(c.g)-q.g)<4&&std::abs(int(c.b)-q.b)<4;}
  if(flat&&cell%8!=0){++totals.skipped;return true;}
 }
 if(!moving||!s.settings.temporal||(cell+frame)%s.settings.refreshRate==0)return false;
 if(ages[eye][std::min(H-1,y+block/2)*W+std::min(W-1,x+block/2)]){++totals.reused;return true;}return false;
}
bool Renderer::fitGradient(Scene& s)const{
 if(!done||moving)return false;
 std::vector<float> values;
 for(int e=0;e<activeEyes;++e)for(int y=0;y<H;y+=block)for(int x=0;x<W;x+=block){const auto& record=shades[e][y*W+x];if(record.valid&&!record.background){float trap=record.trap;values.push_back(s.settings.boundedGradient?trap/(1+trap):trap);}}
 if(values.size()<2)return false;
 std::sort(values.begin(),values.end());
 float lo=values[values.size()/20],hi=values[values.size()-1-values.size()/20];if(hi-lo<1e-7f)return false;
 s.settings.gradientScale=std::min(10000.f,1.f/(hi-lo));s.settings.gradientOffset=-lo*s.settings.gradientScale;s.settings.gradientRepeat=false;return true;
}
void Renderer::applyBloom(const Settings& s,int eyes){
 if(s.bloom<=0)return;
 std::vector<Vec> horizontal(W*H);
 for(int e=0;e<eyes;++e){
  auto bright=[&](Color c){Vec v{c.r/255.f,c.g/255.f,c.b/255.f};float peak=std::fmax(v.x,std::fmax(v.y,v.z));return v*(peak>.6f?(peak-.6f)/peak:0.f);};
  for(int y=0;y<H;++y){Vec sum;for(int dx=-6;dx<=6;++dx)sum=sum+bright(pixels[e][y*W+std::max(0,std::min(W-1,dx))]);
   for(int x=0;x<W;++x){horizontal[y*W+x]=sum*(1.f/13);sum=sum-bright(pixels[e][y*W+std::max(0,x-6)])+bright(pixels[e][y*W+std::min(W-1,x+7)]);}}
  for(int x=0;x<W;++x){Vec sum;for(int dy=-6;dy<=6;++dy)sum=sum+horizontal[std::max(0,std::min(H-1,dy))*W+x];
   for(int y=0;y<H;++y){Color c=pixels[e][y*W+x];pixels[e][y*W+x]=quantize(Vec{c.r/255.f,c.g/255.f,c.b/255.f}+sum*(s.bloom/13));sum=sum-horizontal[std::max(0,y-6)*W+x]+horizontal[std::min(H-1,y+7)*W+x];}}
 }
 ++revision;
}
void Renderer::classifyEmpty(const Settings& s,int eyes){
 if(!s.adaptiveEmpty||s.quality||moving)return;
 emptyBlock=block;emptyColumns=(W+block-1)/block;emptyRows=(H+block-1)/block;++emptyPass;
 for(int eye=0;eye<eyes;++eye){emptyMask[eye].assign(emptyColumns*emptyRows,0);
  for(int y=0;y<emptyRows;++y)for(int x=0;x<emptyColumns;++x){int sky=0;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int sx=std::max(0,std::min(emptyColumns-1,x+dx)),sy=std::max(0,std::min(emptyRows-1,y+dy));const auto& record=shades[eye][sy*block*W+sx*block];if(record.background&&record.escaped)++sky;}
   const auto& center=shades[eye][y*block*W+x*block];
   emptyMask[eye][y*emptyColumns+x]=sky==9?1:s.adaptiveSkyEdges&&sky>=s.skyNeighbors&&center.background&&center.escaped?2:0;
  }
 }
}
void Renderer::present(const Settings& s,int eyes){
 if(s.upscale==0||block==1){presentedReady=false;return;}
 for(int eye=0;eye<eyes;++eye)upscaleImage(pixels[eye],presented[eye],block,s.upscale,s.parallel?parallelFor:nullptr,loopContext);
 presentedReady=true;++revision;
}
void Renderer::finishPass(const Scene& s,int eyes){
 if(!passTiming)return;
 classifyEmpty(s.settings,eyes);
 if(!moving)applyBloom(s.settings,eyes);
 present(s.settings,eyes);
 lastPassMs=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-passStart).count();lastPassBlock=block;lastPassEyes=eyes;
 if(moving){lastLiveMs=lastPassMs;lastLiveBlock=block;}
 passTiming=false;
}
void Renderer::step(const Scene& s,const Rays& rays,float slider){
 if(done)return;
 if(!passTiming){passStart=std::chrono::steady_clock::now();passTiming=true;}
 const auto& o=s.settings;bool stereo=o.stereo&&slider>.001f&&o.eyeSeparation>0;int eyes=stereo?2:1;activeEyes=eyes;
 RenderJob jobs[32];RenderResult results[32];int count=0,limit=std::min(32,std::max(eyes,std::min(o.batchSize,batchLimit)));
 bool accumulate=!moving&&progressiveActive(o)&&(block<=lightingTarget(o)||!o.autoRefine);
 if(accumulate&&accumulation[0].empty())for(auto& sum:accumulation)sum.resize(W*H);
 int nx=(W+block-1)/block,total=nx*((H+block-1)/block),lanes=std::min(o.interlace,total),scan=0;
 while(count+eyes<=limit&&scan++<32){int cell=lane+index*lanes;
  if(cell>=total){++lane;index=0;if(lane>=lanes){lane=0;
   if((!moving&&block>(progressiveActive(o)?lightingTarget(o):(o.quality?1:o.stillBlock))&&o.autoRefine)||accumulate){
    // Commit the last batch before advancing a refinement/accumulation pass.
    if(count){lane=lanes-1;index=(total+lanes-1)/lanes;break;}
    if(!moving&&block>(progressiveActive(o)?lightingTarget(o):(o.quality?1:o.stillBlock))&&o.autoRefine){finishPass(s,eyes);block=std::max(1,block/2);return;}
    finishPass(s,eyes);finishedLightingPasses=lightingPass+1;
    if(finishedLightingPasses<o.lightingPasses){++lightingPass;return;}
   }
   done=true;cacheReady=!moving&&o.bloom<=0&&(!o.quality||o.samples==1)&&!o.adaptiveTiles&&!accumulate&&!(o.dof&&o.aperture>0);break;
  }continue;}
  int x=cell%nx*block,y=cell/nx*block;++index;int sampleBlock=block;
  if(moving&&o.foveated&&!o.quality){int gx=x/(2*block)*(2*block),gy=y/(2*block)*(2*block);float dx=(gx+block-W*.5f)/(W*.5f),dy=(gy+block-H*.5f)/(H*.5f);
   if(dx*dx+dy*dy>.35f){if(x!=gx||y!=gy){totals.skipped+=eyes;continue;}sampleBlock=block*2;}}

  for(int e=0;e<eyes;++e){if(sampleBlock==block&&skipCell(s,x,y,e,cell))continue;
   jobs[count++]={x,y,sampleBlock,e,stereo?(e?1.f:-1.f)*o.eyeSeparation*slider*.5f:0,moving&&o.previewLighting,moving,uint32_t(lightingPass),accumulate};}
 }
 if(!count){if(done)finishPass(s,eyes);return;}
 if(accumulate)presentedReady=false;
 if(batchShader&&o.parallel&&count>1)batchShader(s,rays,jobs,results,count,shaderContext);else renderJobs(s,rays,jobs,results,count);
 ++totals.batches;if(!presentedReady)++revision;
 for(int i=0;i<count;++i){const auto& j=jobs[i];const auto& r=results[i];totals.add(r.profile);rayCount+=r.profile.rays;
  Color display=r.color;
  if(j.accumulate){auto& sum=accumulation[j.eye][j.y*W+j.x];sum=j.sample==0?r.radiance:sum+r.radiance;display=quantize(sum*(1.f/(j.sample+1)));}
  for(int yy=j.y;yy<std::min(j.y+j.block,H);++yy){int first=yy*W+j.x,last=yy*W+std::min(j.x+j.block,W);
   std::fill(pixels[j.eye].begin()+first,pixels[j.eye].begin()+last,display);
   if(o.temporal||o.adaptiveTiles||o.gpuCache)std::fill(depths[j.eye].begin()+first,depths[j.eye].begin()+last,r.depth);
   if(o.temporal)std::fill(ages[j.eye].begin()+first,ages[j.eye].begin()+last,0);
  }
  // Only final-pass block anchors are read by the material cache.
  if(!moving)shades[j.eye][j.y*W+j.x]=r.shade;
  if(r.hit&&o.temporal&&!j.accumulate&&!(o.dof&&o.aperture>0&&!j.moving)){history[historyCursor]={r.point,r.color,r.depth*rays.tangent*j.block/H,frame,j.eye};historyCursor=(historyCursor+1)%history.size();historyCount=std::min(history.size(),historyCount+1);}
 }
 if(done)finishPass(s,eyes);
}
float Renderer::progress(const Scene& s)const{if(done)return 1;int total=((W+block-1)/block)*((H+block-1)/block),lanes=std::min(s.settings.interlace,total);float pass=clamp(float(lane)/lanes+float(index)/total,0,1);if(!moving&&progressiveActive(s.settings)&&(block<=lightingTarget(s.settings)||!s.settings.autoRefine))return (lightingPass+pass)/s.settings.lightingPasses;return pass;}
}
