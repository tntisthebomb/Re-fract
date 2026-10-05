#include "engine.hpp"
#include "ui.hpp"
#include <fstream>
#include <iostream>
using namespace rf;
int main(int argc,char** argv){
 if(argc!=2)return 1;std::string prefix=argv[1],error;Scene s;s.formula=preset(36);s.camera=presetCamera(36);s.settings=presetSettings(36);s.settings.stereo=false;s.settings.autoRefine=false;s.settings.previewBlock=2;
 Renderer renderer;
 auto render=[&](const char* name){renderer.invalidate(s,false);Rays rays(s);while(!renderer.complete())renderer.step(s,rays,0);if(!saveJPEG(renderer.image(0),W,H,prefix+name+".jpg",error)||!savePPM(renderer.image(0),prefix+name+".ppm",error)){std::cerr<<error;return false;}return true;};
 s.settings.boundedGradient=false;if(!render("-old"))return 1;
 s.settings.boundedGradient=true;s.settings.gradientStops=5;s.settings.gradientLow={.02f,.015f,.08f};s.settings.gradientMiddle={{{.1f,.12f,.8f},{.85f,.02f,.3f},{1,.65f,.05f}}};s.settings.gradientHigh={.15f,1,.7f};
 if(!renderer.fitGradient(s))return 2;if(!render("-gradient"))return 1;
 s.settings.emission=1.2f;s.settings.bloom=1.5f;if(!render("-glow"))return 1;
 s.settings.stereo=true;renderer.invalidate(s,false);Rays stereo(s);while(!renderer.complete())renderer.step(s,stereo,1);if(!saveMPO(renderer.image(0),renderer.image(1),prefix+"-stereo.mpo",error))return 1;
 Canvas picker(320,240);drawColorPicker(picker,.76f,.8f,.9f);std::ofstream out(prefix+"-picker.ppm",std::ios::binary);out<<"P6\n320 240\n255\n";out.write(reinterpret_cast<char*>(picker.pixels.data()),picker.pixels.size()*sizeof(Color));
 std::cout<<"Gradient scale "<<s.settings.gradientScale<<" offset "<<s.settings.gradientOffset<<"; last 2X pass "<<renderer.lastCompletedFrameMs()<<" ms (desktop)\n";
}
