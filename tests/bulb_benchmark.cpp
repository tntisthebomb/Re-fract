#include "engine.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
using namespace rf;
int main(){
 std::cout<<"preset,exact_ms,algebraic_ms,changed_pixels,mean_channel_error,max_channel_error\n";
 for(int n:{0,1,5}){Scene s;s.formula=preset(n);s.camera=presetCamera(n);s.settings.farClip=40;s.settings.convergence=-s.camera.position.z;Rays rays(s);
  std::vector<Color> images[2];std::vector<double> measurements[2];double times[2]={};
  for(int run=0;run<12;++run)for(int k=0;k<2;++k){int mode=(run&1)?1-k:k;s.formula.algebraicBulb=mode;images[mode].clear();auto start=std::chrono::steady_clock::now();
   for(int y=0;y<H;y+=8)for(int x=0;x<W;x+=8)images[mode].push_back(trace(s,rays,x+4,y+4,0));
   double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(run>=2)measurements[mode].push_back(elapsed);
  }
  for(int mode=0;mode<2;++mode){std::sort(measurements[mode].begin(),measurements[mode].end());times[mode]=(measurements[mode][4]+measurements[mode][5])*.5;}
  int changed=0,maximum=0;double error=0;
  for(size_t i=0;i<images[0].size();++i){Color a=images[0][i],b=images[1][i];int r=std::abs(int(a.r)-b.r),g=std::abs(int(a.g)-b.g),c=std::abs(int(a.b)-b.b);changed+=r||g||c;error+=r+g+c;maximum=std::max(maximum,std::max(r,std::max(g,c)));}
  std::cout<<n<<','<<times[0]<<','<<times[1]<<','<<changed<<','<<error/(images[0].size()*3)<<','<<maximum<<'\n';
 }
}
