#include "engine.hpp"
#include <chrono>
#include <iostream>
#include <memory>
using namespace rf;
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: optics_preview output-prefix\n";return 1;}
 std::cout<<"case,desktop_ms,rays,de_queries,passes\n";
 for(int mode=0;mode<8;++mode){Scene s;int n=mode>=3&&mode<=5?32:25;s.formula=preset(n);s.camera=presetCamera(n);s.settings=presetSettings(n);s.settings.quality=true;s.settings.autoRefine=false;s.settings.stereo=false;
  if(mode<=2){s.settings.sunStrength=.3f;
   if(mode>0){for(int i=0;i<2;++i){auto& lamp=s.settings.pointLights[i];lamp.enabled=true;lamp.intensity=5;lamp.range=8;}
    s.settings.pointLights[0].position={-1,.8f,1};s.settings.pointLights[0].color={1,.3f,.15f};
    s.settings.pointLights[1].position={1,-.2f,2};s.settings.pointLights[1].color={.15f,.5f,1};
    if(mode==2){s.settings.pointShadows=true;s.settings.shadow=24;}
   }
  }
  if(mode==4||mode==5){s.settings.dof=true;s.settings.aperture=.04f;s.settings.focusDistance=mode==4?.75f:4;s.settings.dofSamples=8;}
  if(mode>=6){s.settings.giSamples=1;s.settings.giStrength=1.5f;s.settings.sunStrength=.2f;s.settings.giSteps=24;s.settings.progressiveLighting=true;s.settings.lightingPasses=mode==6?1:16;}
  auto renderer=std::make_unique<Renderer>();Rays rays(s);renderer->invalidate(s,false);auto start=std::chrono::steady_clock::now();
  while(!renderer->complete())renderer->step(s,rays,0);
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::string error;if(!saveScene(s,std::string(argv[1])+"-"+std::to_string(mode)+".rfs",error)){std::cerr<<error<<'\n';return 1;}if(!savePPM(renderer->image(0),std::string(argv[1])+"-"+std::to_string(mode)+".ppm",error)){std::cerr<<error<<'\n';return 1;}
  std::cout<<mode<<','<<ms<<','<<renderer->rays()<<','<<renderer->profile().distanceQueries<<','<<renderer->accumulatedPasses()<<'\n';
 }
}
