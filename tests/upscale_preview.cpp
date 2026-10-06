#include "engine.hpp"
#include <iostream>
using namespace rf;
int main(int argc,char** argv){if(argc!=2)return 1;std::string prefix=argv[1],error;Scene s;s.formula=preset(36);s.camera=presetCamera(36);s.settings=presetSettings(36);s.settings.stereo=false;s.settings.skyColor={.06f,.1f,.3f};s.settings.previewBlock=4;s.settings.stillBlock=2;Renderer renderer;
 for(int mode=0;mode<3;++mode){s.settings.upscale=mode;renderer.invalidate(s,false);Rays rays(s);while(!renderer.complete())renderer.step(s,rays,0);if(!savePPM(renderer.image(0),prefix+"-"+std::to_string(mode)+".ppm",error))return 2;std::cout<<"Mode "<<mode<<": last 2X pass "<<renderer.lastCompletedFrameMs()<<" ms (desktop)\n";}
 s.settings.skyColor={.4f,.025f,.035f};renderer.invalidate(s,false);Rays rays(s);while(!renderer.complete())renderer.step(s,rays,0);if(!savePPM(renderer.image(0),prefix+"-sky.ppm",error))return 2;
}
