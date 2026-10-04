#include "trace_benchmark.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
using namespace rf;
using Clock=std::chrono::steady_clock;
int main(){
 std::cout<<"preset,mode,median_ms,de_queries,cache_queries,checksum,field_build_ms,cache_changed_pixels,cache_max_channel_delta\n";
 for(int n:{0,6,7,24}){
  Scene baseline;baseline.formula=preset(n);baseline.camera=presetCamera(n);baseline.settings=presetSettings(n);
  auto start=Clock::now();auto field=std::make_shared<DistanceField>(baseline);while(!field->ready())field->build();
  double build=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  const char* names[]={"moving exact","moving field","stationary default","stationary zoom precision","stationary indirect 1","stationary indirect 4"};
  for(int mode=0;mode<6;++mode){std::vector<double> times;uint64_t checksum=0,queries=0,reused=0;
   for(int run=0;run<6;++run){Scene scene=baseline;if(mode==3)scene.settings.adaptivePrecision=true;
    if(mode>=4)scene.settings.giSamples=mode==4?1:4;
    TraceBenchmark bench(scene,mode==1?field:nullptr,mode<2);start=Clock::now();while(!bench.complete())bench.step();
    double elapsed=std::chrono::duration<double,std::milli>(Clock::now()-start).count();if(run)times.push_back(elapsed);
    checksum=bench.digest();queries=bench.profile().distanceQueries;reused=bench.profile().reused;
   }
   int changed=0,maximum=0;
   if(mode==1){Rays exact(baseline),cached(baseline);cached.field=field.get();
    for(int cell=0;cell<1500;++cell){RenderJob job{cell%50*8+4,cell/50*8+4,1,0,0,true};auto a=renderJob(baseline,exact,job).color,b=renderJob(baseline,cached,job).color;
     int delta=std::max(std::abs(int(a.r)-b.r),std::max(std::abs(int(a.g)-b.g),std::abs(int(a.b)-b.b)));changed+=delta!=0;maximum=std::max(maximum,delta);}
   }
   std::sort(times.begin(),times.end());std::cout<<n<<','<<names[mode]<<','<<times[2]<<','<<queries<<','<<reused<<','<<checksum<<','<<(mode==1?build:0)<<','<<changed<<','<<maximum<<'\n';
  }
 }
}
