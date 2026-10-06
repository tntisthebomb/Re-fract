#include "engine.hpp"
#include <chrono>
#include <iostream>
using namespace rf;
int main(){
 std::cout<<"preset,mode,host_ms,primary_rays,de_queries,cached_hits,gi_skips,depth_starts,budget_retries\n";
 for(int presetIndex:{0,36,37})for(int mode=0;mode<5;++mode){
  Scene s;s.formula=preset(presetIndex);s.camera=presetCamera(presetIndex);s.settings=presetSettings(presetIndex);
  auto& o=s.settings;o.stereo=false;o.previewBlock=8;o.stillBlock=4;o.lightingBlock=4;o.giSamples=1;o.giSteps=12;o.progressiveLighting=true;o.lightingPasses=8;
  if(mode>=1)o.separateLighting=true;
  if(mode>=2)o.lightingBlock=8;
  if(mode==3){o.adaptiveLighting=true;o.lightingMinPasses=4;o.lightingThreshold=.03f;}
  if(mode==4){o.depthPrepass=true;o.prepassBlock=16;o.adaptiveRayBudget=true;o.rayBudgetRetry=false;}
  Renderer r;r.invalidate(s,false);Rays rays(s);auto start=std::chrono::steady_clock::now();while(!r.complete())r.step(s,rays,0);
  auto& p=r.profile();double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  const char* names[]={"full","cached","coarse_gi","adaptive_gi","depth_and_short_budget"};
  std::cout<<presetIndex<<','<<names[mode]<<','<<ms<<','<<p.rays<<','<<p.distanceQueries<<','<<p.reused<<','<<p.lightingSkipped<<','<<p.depthStarts<<','<<p.budgetRetries<<'\n';
 }
}
