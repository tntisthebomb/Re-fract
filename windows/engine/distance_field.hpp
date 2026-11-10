#pragma once
#include "engine.hpp"
#include <algorithm>
namespace rf {
// Experimental nearest-sample distance bounds for moving previews only.
// Arbitrary formula DEs are not proven Lipschitz fields: never use this for final rendering.
class DistanceField {
 static constexpr int Side=33;
 Formula formula;Vec center;float spacing;int row=0;
 std::vector<float> values;
 public:
 explicit DistanceField(const Scene& s):formula(s.formula),center(s.camera.position),spacing(s.settings.fieldSpacing),values(Side*Side*Side,-1){}
 bool ready()const{return row==Side*Side;}
 float progress()const{return float(row)/(Side*Side);}
 bool containsCamera(Vec p)const{Vec d=p-center;float bound=spacing*12;return std::fabs(d.x)<bound&&std::fabs(d.y)<bound&&std::fabs(d.z)<bound;}
 void build(ParallelFor loop=nullptr,void* context=nullptr){
  int count=std::min(4,Side*Side-row);if(count==0)return;
  auto body=[](int offset,void* data){auto& f=*static_cast<DistanceField*>(data);int k=f.row+offset,y=k%Side,z=k/Side;
   for(int x=0;x<Side;++x){Vec p=f.center+Vec{float(x-16),float(y-16),float(z-16)}*f.spacing;auto d=distanceOnly(f.formula,p);f.values[k*Side+x]=d.valid?d.distance:-1;}
  };
  if(loop)loop(count,body,this,context);else for(int k=0;k<count;++k)body(k,this);
  row+=count;
 }
 bool lookup(Vec p,float& distance)const{
  if(!ready())return false;
  Vec relative=(p-center)*(1/spacing)+Vec{16,16,16};
  if(!std::isfinite(relative.x)||!std::isfinite(relative.y)||!std::isfinite(relative.z))return false;
  if(relative.x<0||relative.x>32||relative.y<0||relative.y>32||relative.z<0||relative.z>32)return false;
  int x=int(relative.x+.5f),y=int(relative.y+.5f),z=int(relative.z+.5f);
  float sample=values[(z*Side+y)*Side+x];
  // Near geometry, use the exact formula; this cache only skips open-space queries.
  if(sample<=spacing*2)return false;
  float offset=(relative-Vec{float(x),float(y),float(z)}).length()*spacing;
  distance=sample-offset;return distance>spacing;
 }
};
}
