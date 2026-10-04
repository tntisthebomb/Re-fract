#pragma once
#include "engine.hpp"
#include "distance_field.hpp"
#include <memory>
#include <algorithm>
#include <fstream>
#include <iomanip>

namespace rf {
// A fixed mono sample grid of a frozen scene, independent of display/refinement work.
class TraceBenchmark {
 Scene scene;Rays rays;std::shared_ptr<const DistanceField> field;bool preview=false;int cursor=0;float elapsedMs=0;uint64_t checksum=0;Profile totals;
 public:
 explicit TraceBenchmark(const Scene& snapshot,std::shared_ptr<const DistanceField> cache={},bool moving=false):scene(snapshot),rays(scene),field(std::move(cache)),preview(moving){
  scene.formula.iterations=renderingIterations(snapshot,moving);
  if(field&&field->ready()&&!scene.settings.adaptiveDetail)rays.field=field.get();
 }
 bool complete()const{return cursor==1500;}
 int completedSamples()const{return cursor;}
 void step(BatchShader shader=nullptr,void* context=nullptr){
  RenderJob jobs[16];RenderResult results[16];int count=std::min(16,1500-cursor);
  for(int k=0;k<count;++k){int cell=cursor+k;jobs[k]={cell%50*8+4,cell/50*8+4,1,0,0,preview};}
  if(!count)return;
  if(shader&&scene.settings.parallel)shader(scene,rays,jobs,results,count,context);
  else renderJobs(scene,rays,jobs,results,count);
  for(int k=0;k<count;++k){const auto& r=results[k];totals.add(r.profile);checksum=checksum*131+r.color.r*65536+r.color.g*256+r.color.b;}
  cursor+=count;
 }
 void recordMs(float ms){if(std::isfinite(ms)&&ms>=0)elapsedMs+=ms;}
 float raysPerSecond()const{return elapsedMs>0?totals.rays*1000.f/elapsedMs:0;}
 uint64_t digest()const{return checksum;}
 const Profile& profile()const{return totals;}
 bool save(const std::string& path)const{
  std::ofstream out(path);if(!out)return false;
  const auto& s=scene.settings;
  out<<std::setprecision(9)<<"samples,rays,trace_ms,rays_per_second,de_queries,checksum,iterations,ray_steps,quality,parallel,epsilon,zoom_precision,gi_samples,gi_steps,algebraic_bulb,moving_preview,distance_field_active\n"
   <<cursor<<','<<totals.rays<<','<<elapsedMs<<','<<raysPerSecond()<<','<<totals.distanceQueries<<','<<checksum<<','
   <<scene.formula.iterations<<','<<s.steps<<','<<s.quality<<','<<s.parallel<<','<<s.epsilon<<','<<s.adaptivePrecision<<','
   <<s.giSamples<<','<<s.giSteps<<','<<scene.formula.algebraicBulb<<','<<preview<<','<<(rays.field!=nullptr)<<'\n';
  out.close();return bool(out);
 }
};
}
