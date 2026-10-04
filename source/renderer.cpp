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

}
Color recolorSample(const Settings& o,const ShadeRecord& r){
 Vec background{.025f,.028f,.024f};float fog=std::exp(-r.depth*o.fog);
 Vec color=gradientColor(o,r.trap)*r.lighting+Vec{r.specular,r.specular,r.specular};
 return quantize((color*fog+background*(1-fog))*o.exposure);
}
static RenderResult sampleRay(const Scene& s,const Rays& rays,float x,float y,float eye,bool fast){
 RenderResult result;result.profile.rays=1;
 // Per-ray counts fit in 32 bits; publish 64-bit totals once, avoiding paired
 // loads/stores on ARM11 for every distance-estimator call.
 uint32_t queries=0,shadingQueries=0,stepsTaken=0;
 auto query=[&](Vec p,bool shading=false){++queries;if(shading)++shadingQueries;return distanceOnly(s.formula,p,s.settings.quality);};
 auto finish=[&](Color c){result.color=c;result.profile.distanceQueries=queries;result.profile.shadingQueries=shadingQueries;result.profile.steps=stepsTaken;return result;};
 Vec origin,dir;rays.ray(s,x,y,eye,origin,dir);
 float t=0,trap=0;bool hit=false;const Settings& o=s.settings;
 float previousT=0,previousD=0,advance=0;bool relaxed=false;
 int steps=o.quality?std::min(512,o.steps*2):o.steps;
 for(int i=0;i<steps&&t<o.farClip;++i){
  ++stepsTaken;Sample d=query(origin+dir*t);
  if(!d.valid)return finish({180,20,90});
  if(relaxed&&previousD+d.distance<advance){t=previousT+std::fmax(previousD*o.safety,o.epsilon*.25f);d=query(origin+dir*t);++result.profile.relaxFallbacks;if(!d.valid)return finish({180,20,90});relaxed=false;}
  float eps=o.epsilon*(o.quality?.5f:1.f)*std::fmax(1.f,t*.2f);
  if(d.distance<eps){hit=true;trap=d.trap;break;}
  previousT=t;previousD=d.distance;advance=std::fmax(d.distance*o.safety,eps*.25f);
  relaxed=!o.quality&&!s.formula.repeat&&o.relaxation>1; if(relaxed)advance*=o.relaxation;
  // Stop at a periodic-cell boundary before tracing the next cell's geometry.
  if(s.formula.repeat)advance=std::fmin(advance,repeatBoundaryStep(s.formula,origin+dir*t,dir)+eps*.25f);
  t+=advance;
 }
 Vec background{.025f,.028f,.024f};
 if(!hit)return finish(quantize(background+Vec{.018f,.012f,.006f}*(1-y/H)));
 Vec p=origin+dir*t;result.hit=true;result.point=p;result.depth=t;
 ++queries;++shadingQueries;trap=distance(s.formula,p,o.quality).trap;
 if(fast){float fog=std::exp(-t*o.fog);return finish(quantize((gradientColor(o,trap)*fog+background*(1-fog))*o.exposure));}
 float e=std::fmax(o.epsilon,t*o.epsilon*.25f);
 // Tetrahedral normal: four DE evaluations instead of six central differences.
 Vec v1{1,-1,-1},v2{-1,-1,1},v3{-1,1,-1},v4{1,1,1};
 Vec n=(v1*query(p+v1*e,true).distance+v2*query(p+v2*e,true).distance+
        v3*query(p+v3*e,true).distance+v4*query(p+v4*e,true).distance).unit();
 float diffuse=std::fmax(0.f,n.dot(rays.light)),ao=1,shadow=1;
 float baseSpec=o.specular>0?std::pow(std::fmax(0.f,n.dot((rays.light-dir).unit())),24)*o.specular:0;
 for(int i=1;i<=o.ao&&ao>.15f;++i){float h=e*6*i;Sample d=query(p+n*h,true);ao-=std::fmax(0.f,h-d.distance)/h*(.35f/i);}
 if(o.shadow&&(diffuse>0||baseSpec>0)){float st=e*8;Vec start=p+n*(e*4);
  for(int i=0;i<o.shadow&&st<8;++i){Sample d=query(start+rays.light*st,true);
   if(!d.valid||d.distance<e){shadow=.15f;break;}
   shadow=std::fmin(shadow,12*d.distance/st);st+=std::fmax(d.distance*o.safety,e);
  }
 }
 float spec=baseSpec*shadow;
 result.shade={trap,t,clamp(ao,.15f,1)*(.22f+.78f*diffuse*clamp(shadow,.15f,1)),spec,true};
 Vec color=gradientColor(o,trap)*(clamp(ao,.15f,1)*(.22f+.78f*diffuse*clamp(shadow,.15f,1)))+Vec{spec,spec,spec};
 float fog=std::exp(-t*o.fog);color=(color*fog+background*(1-fog))*o.exposure;
 return finish(quantize(color));
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
 float t=trap*s.gradientScale+s.gradientOffset;
 t=s.gradientRepeat?t-std::floor(t):clamp(t,0,1);
 Vec lo=s.gradientLow,hi=s.gradientHigh;
 if(!s.customGradient){int n=int(s.palette)%4;
  if(n==1){lo={.13f,.4f,.48f};hi={.72f,.85f,.55f};}
  else if(n==2){lo={.3f,.09f,.36f};hi={.95f,.43f,.63f};}
  else if(n==3){lo={.18f,.19f,.17f};hi={.85f,.86f,.73f};}
  else{lo={.18f,.06f,.025f};hi={.95f,.58f,.22f};}
 }
 return lo*(1-t)+hi*t;
}
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye){return sampleRay(s,rays,x,y,eye,false).color;}
void Profile::add(const Profile& p){rays+=p.rays;steps+=p.steps;distanceQueries+=p.distanceQueries;shadingQueries+=p.shadingQueries;reused+=p.reused;skipped+=p.skipped;relaxFallbacks+=p.relaxFallbacks;batches+=p.batches;}
RenderResult renderJob(const Scene& s,const Rays& rays,const RenderJob& j){
 RenderResult result;int red=0,green=0,blue=0,count=s.settings.quality?s.settings.samples:1;
 for(int i=0;i<count;++i){float jx=count==1?.5f:(i&1)?.75f:.25f,jy=count<=2?.5f:(i&2)?.75f:.25f;
  auto r=sampleRay(s,rays,std::min(W-.5f,j.x+j.block*jx),std::min(H-.5f,j.y+j.block*jy),j.offset,j.fast&&!s.settings.quality);
  if(i==0){result.shade=r.shade;result.point=r.point;result.depth=r.depth;result.hit=r.hit;}
  result.profile.add(r.profile);red+=r.color.r;green+=r.color.g;blue+=r.color.b;
 }
 if(count!=1)result.shade.valid=false;
 result.color={uint8_t(red/count),uint8_t(green/count),uint8_t(blue/count)};return result;
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
 cacheReady=false;
 int next=motion?(s.settings.adaptiveResolution?dynamicBlock:s.settings.previewBlock):(s.settings.quality?1:s.settings.previewBlock);
 if(!(motion&&moving)||next!=block){index=0;lane=0;}
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
 if(eyes!=activeEyes||offset!=eyeOffset){historyCount=historyCursor=0;for(auto& a:ages)std::fill(a.begin(),a.end(),0);}
 activeEyes=eyes;eyeOffset=offset;
 if(changed){dynamicBlock=s.settings.previewBlock;invalidate(s,motion,true);}
 else if(motion||moving)invalidate(s,motion,false);
 if(motion&&s.settings.temporal&&!s.settings.quality)reproject(s,Rays(s),slider);
}
bool Renderer::recolor(const Scene& s){
 if(!cacheReady||!done||moving)return false;
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
 historyCount=historyCursor=0;++revision;return true;
}
bool Renderer::captureSurface(const Scene& s,SurfaceMesh& mesh)const{
 if(!s.settings.gpuCache||!cacheReady||!done||moving||block>s.settings.meshStride)return false;
 SurfaceMesh next=surfaceMesh(s,pixels[0],depths[0],block,activeEyes==2?-eyeOffset:0,parallelFor,loopContext);
 if(next.indices.empty())return false;
 mesh=std::move(next);return true;
}
void Renderer::endFrame(const Scene& s,float totalMs,float renderMs,uint64_t jobs){
 frameMs=totalMs;if(jobs){float cost=renderMs/jobs;averageJobMs=averageJobMs==0?cost:averageJobMs*.8f+cost*.2f;}
 batchLimit=std::max(1,std::min(s.settings.batchSize,int(s.settings.budgetMs*.5f/std::max(.001f,averageJobMs))));
 if(moving&&s.settings.adaptiveResolution&&frame%15==0){float target=1000.f/s.settings.targetFps;
 float estimate=averageJobMs*((W+dynamicBlock-1)/dynamicBlock)*((H+dynamicBlock-1)/dynamicBlock)*activeEyes;
 if(estimate>target*2)dynamicBlock=std::min(32,dynamicBlock*2);
 else if(estimate*4<target*1.4f)dynamicBlock=std::max(4,dynamicBlock/2);}
}
bool Renderer::skipCell(const Scene& s,int x,int y,int eye,int cell){
 if(s.settings.quality)return false;
 if(!moving&&s.settings.adaptiveTiles&&block<4){
  int cx=std::min(W-1,x+block/2),cy=std::min(H-1,y+block/2);float center=depths[eye][cy*W+cx];Color c=pixels[eye][cy*W+cx];bool flat=center>0;
  for(int dy:{-4,4})for(int dx:{-4,4}){int k=std::max(0,std::min(H-1,cy+dy))*W+std::max(0,std::min(W-1,cx+dx));Color q=pixels[eye][k];
   flat=flat&&depths[eye][k]>0&&std::fabs(depths[eye][k]-center)<center*.005f&&std::abs(int(c.r)-q.r)<4&&std::abs(int(c.g)-q.g)<4&&std::abs(int(c.b)-q.b)<4;}
  if(flat&&cell%8!=0){++totals.skipped;return true;}
 }
 if(!moving||!s.settings.temporal||(cell+frame)%s.settings.refreshRate==0)return false;
 if(ages[eye][std::min(H-1,y+block/2)*W+std::min(W-1,x+block/2)]){++totals.reused;return true;}return false;
}
void Renderer::step(const Scene& s,const Rays& rays,float slider){
 if(done)return;
 const auto& o=s.settings;bool stereo=o.stereo&&slider>.001f&&o.eyeSeparation>0;int eyes=stereo?2:1;activeEyes=eyes;
 RenderJob jobs[32];RenderResult results[32];int count=0,limit=std::min(32,std::max(eyes,std::min(o.batchSize,batchLimit)));
 int nx=(W+block-1)/block,total=nx*((H+block-1)/block),lanes=std::min(o.interlace,total),scan=0;
 while(count+eyes<=limit&&scan++<32){int cell=lane+index*lanes;
  if(cell>=total){++lane;index=0;if(lane>=lanes){lane=0;if(!moving&&block>1&&o.autoRefine){if(count){lane=lanes-1;index=(total+lanes-1)/lanes;break;}block=std::max(1,block/2);return;}done=true;cacheReady=!moving&&(!o.quality||o.samples==1)&&!o.adaptiveTiles;break;}continue;}
  int x=cell%nx*block,y=cell/nx*block;++index;int sampleBlock=block;
  if(moving&&o.foveated&&!o.quality){int gx=x/(2*block)*(2*block),gy=y/(2*block)*(2*block);float dx=(gx+block-W*.5f)/(W*.5f),dy=(gy+block-H*.5f)/(H*.5f);
   if(dx*dx+dy*dy>.35f){if(x!=gx||y!=gy){totals.skipped+=eyes;continue;}sampleBlock=block*2;}}

  for(int e=0;e<eyes;++e){if(sampleBlock==block&&skipCell(s,x,y,e,cell))continue;
   jobs[count++]={x,y,sampleBlock,e,stereo?(e?1.f:-1.f)*o.eyeSeparation*slider*.5f:0,moving&&o.previewLighting};}
 }
 if(!count)return;
 if(batchShader&&o.parallel&&count>1)batchShader(s,rays,jobs,results,count,shaderContext);else renderJobs(s,rays,jobs,results,count);
 ++totals.batches;++revision;
 for(int i=0;i<count;++i){const auto& j=jobs[i];const auto& r=results[i];totals.add(r.profile);rayCount+=r.profile.rays;
  for(int yy=j.y;yy<std::min(j.y+j.block,H);++yy){int first=yy*W+j.x,last=yy*W+std::min(j.x+j.block,W);
   std::fill(pixels[j.eye].begin()+first,pixels[j.eye].begin()+last,r.color);
   if(o.temporal||o.adaptiveTiles||o.gpuCache)std::fill(depths[j.eye].begin()+first,depths[j.eye].begin()+last,r.depth);
   if(o.temporal)std::fill(ages[j.eye].begin()+first,ages[j.eye].begin()+last,0);
  }
  // Only final-pass block anchors are read by the material cache.
  if(!moving)shades[j.eye][j.y*W+j.x]=r.shade;
  if(r.hit&&o.temporal){history[historyCursor]={r.point,r.color,r.depth*rays.tangent*j.block/H,frame,j.eye};historyCursor=(historyCursor+1)%history.size();historyCount=std::min(history.size(),historyCount+1);}
 }
}
float Renderer::progress(const Scene& s)const{if(done)return 1;int total=((W+block-1)/block)*((H+block-1)/block),lanes=std::min(s.settings.interlace,total);return clamp(float(lane)/lanes+float(index)/total,0,1);}
}
