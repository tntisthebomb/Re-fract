#include "engine.hpp"
#include <fstream>
#include <iomanip>
#include <cstdio>

namespace rf {
namespace {
bool settingsValid(const Scene& s){
 const Settings& v=s.settings;
 auto range=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
 return v.steps>=8&&v.steps<=256&&(v.previewBlock==4||v.previewBlock==8||v.previewBlock==16)&&v.interlace>=1&&v.interlace<=8&&
 v.ao>=0&&v.ao<=6&&v.shadow>=0&&v.shadow<=64&&(v.samples==1||v.samples==2||v.samples==4)&&
 range(v.epsilon,.0001f,.05f)&&range(v.safety,.05f,1)&&range(v.farClip,1,100)&&range(v.fov,20,100)&&
 range(v.eyeSeparation,0,.2f)&&range(v.convergence,.2f,30)&&range(v.exposure,.1f,4)&&range(v.fog,0,1)&&
 range(v.lightYaw,-6.3f,6.3f)&&range(v.lightPitch,-1.5f,1.5f)&&range(v.palette,0,3)&&range(v.specular,0,1)&&range(v.budgetMs,1,20)&&
 range(s.camera.position.x,-10000,10000)&&range(s.camera.position.y,-10000,10000)&&range(s.camera.position.z,-10000,10000)&&
 v.batchSize>=1&&v.batchSize<=32&&v.targetFps>=15&&v.targetFps<=60&&v.historyFrames>=1&&v.historyFrames<=30&&v.refreshRate>=1&&v.refreshRate<=8&&
 range(v.relaxation,1,1.5f)&&range(v.gradientScale,.01f,100)&&range(v.gradientOffset,-100,100)&&
 range(v.gradientLow.x,0,1)&&range(v.gradientLow.y,0,1)&&range(v.gradientLow.z,0,1)&&range(v.gradientHigh.x,0,1)&&range(v.gradientHigh.y,0,1)&&range(v.gradientHigh.z,0,1)&&
 range(s.camera.surfaceRange,.001f,20)&&range(s.camera.minimumSpeed,.0001f,1)&&range(s.camera.yaw,-6.3f,6.3f)&&range(s.camera.pitch,-1.5f,1.5f)&&range(s.camera.speed,.01f,10);
}
}
bool saveScene(const Scene& s,const std::string& path,std::string& error){
 Scene checked=s;if(!checked.formula.validate(error)||!settingsValid(s)){if(error.empty())error="Invalid scene settings";return false;}
 std::ofstream out(path+".tmp");if(!out){error="Cannot write scene";return false;}
 out<<std::setprecision(9)<<"REFRACT 2\n";
 const Formula& f=s.formula;const Settings& v=s.settings;const Camera& c=s.camera;
 out<<f.iterations<<' '<<f.bailout<<' '<<f.logarithmic<<' '<<f.julia<<' '<<f.constant.x<<' '<<f.constant.y<<' '<<f.constant.z<<' '<<f.derivativeScale<<' '<<f.terminal<<' '<<f.terminalRadius<<'\n';
 out<<v.steps<<' '<<v.previewBlock<<' '<<v.interlace<<' '<<v.ao<<' '<<v.shadow<<' '<<v.samples<<' '
 <<v.epsilon<<' '<<v.safety<<' '<<v.farClip<<' '<<v.fov<<' '<<v.eyeSeparation<<' '<<v.convergence<<' '
 <<v.exposure<<' '<<v.fog<<' '<<v.lightYaw<<' '<<v.lightPitch<<' '<<v.palette<<' '<<v.specular<<' '
 <<v.stereo<<' '<<v.quality<<' '<<v.autoRefine<<' '<<v.parallel<<' '<<v.budgetMs<<'\n';
 out<<c.position.x<<' '<<c.position.y<<' '<<c.position.z<<' '<<c.yaw<<' '<<c.pitch<<' '<<c.speed<<'\n';
 out<<v.batchSize<<' '<<v.targetFps<<' '<<v.historyFrames<<' '<<v.refreshRate<<' '<<v.adaptiveResolution<<' '<<v.previewLighting<<' '<<v.temporal<<' '<<v.stereoReuse<<' '<<v.adaptiveTiles<<' '<<v.foveated<<' '<<v.relaxation<<'\n';
 out<<v.customGradient<<' '<<v.gradientRepeat<<' '<<v.gradientLow.x<<' '<<v.gradientLow.y<<' '<<v.gradientLow.z<<' '<<v.gradientHigh.x<<' '<<v.gradientHigh.y<<' '<<v.gradientHigh.z<<' '<<v.gradientScale<<' '<<v.gradientOffset<<'\n';
 out<<c.surfaceSpeed<<' '<<c.surfaceRange<<' '<<c.minimumSpeed<<'\n';
 out<<f.stages.size()<<'\n';
 for(const Stage& a:f.stages){out<<int(a.kind)<<' '<<a.enabled<<' '<<a.a<<' '<<a.b<<' '<<a.c<<'\n';for(const auto& text:a.text)out<<std::quoted(text)<<'\n';}
 out.close();if(!out){error="Scene write failed";std::remove((path+".tmp").c_str());return false;}
 if(std::rename((path+".tmp").c_str(),path.c_str())!=0){error="Cannot replace scene";return false;}
 error="SCENE SAVED";return true;
}
bool loadScene(Scene& s,const std::string& path,std::string& error){
 std::ifstream in(path,std::ios::binary);if(!in){error="Scene slot is empty";return false;}
 in.seekg(0,std::ios::end);auto size=in.tellg();if(size<0||size>16384){error="Scene file too large";return false;}in.seekg(0);
 Scene next;std::string magic;int version=0;in>>magic>>version;
 if(magic!="REFRACT"||(version!=1&&version!=2)){error="Unsupported scene format";return false;}
 Formula& f=next.formula;Settings& v=next.settings;Camera& c=next.camera;
 in>>f.iterations>>f.bailout>>f.logarithmic>>f.julia>>f.constant.x>>f.constant.y>>f.constant.z>>f.derivativeScale>>f.terminal>>f.terminalRadius;
 in>>v.steps>>v.previewBlock>>v.interlace>>v.ao>>v.shadow>>v.samples>>v.epsilon>>v.safety>>v.farClip>>v.fov>>v.eyeSeparation>>v.convergence
 >>v.exposure>>v.fog>>v.lightYaw>>v.lightPitch>>v.palette>>v.specular>>v.stereo>>v.quality>>v.autoRefine>>v.parallel>>v.budgetMs;
 in>>c.position.x>>c.position.y>>c.position.z>>c.yaw>>c.pitch>>c.speed;
 if(version>=2){
 in>>v.batchSize>>v.targetFps>>v.historyFrames>>v.refreshRate>>v.adaptiveResolution>>v.previewLighting>>v.temporal>>v.stereoReuse>>v.adaptiveTiles>>v.foveated>>v.relaxation;
 in>>v.customGradient>>v.gradientRepeat>>v.gradientLow.x>>v.gradientLow.y>>v.gradientLow.z>>v.gradientHigh.x>>v.gradientHigh.y>>v.gradientHigh.z>>v.gradientScale>>v.gradientOffset;
 in>>c.surfaceSpeed>>c.surfaceRange>>c.minimumSpeed;
 }
 int count=0;in>>count;if(count<1||count>MaxStages){error="Invalid stage count";return false;}
 f.stages.clear();for(int i=0;i<count;++i){Stage a;int kind=0;in>>kind>>a.enabled>>a.a>>a.b>>a.c;a.kind=Kind(kind);
  for(auto& text:a.text)in>>std::quoted(text);
  f.stages.push_back(a);
 }
 if(!in){error="Truncated or malformed scene";return false;}in>>std::ws;if(!in.eof()){error="Unexpected trailing data";return false;}
 if(!settingsValid(next)){error="Settings outside supported range";return false;}
 if(!f.validate(error))return false;
 s=next;error="SCENE LOADED";return true;
}
bool savePPM(const std::vector<Color>& pixels,const std::string& path,std::string& error){
 if(pixels.size()!=W*H){error="Invalid image size";return false;}
 std::ofstream out(path,std::ios::binary);if(!out){error="Cannot write image";return false;}
 out<<"P6\n"<<W<<' '<<H<<"\n255\n";
 for(Color c:pixels){char rgb[]={char(c.r),char(c.g),char(c.b)};out.write(rgb,3);}
 out.close();if(!out){error="Image write failed";return false;}error="IMAGE SAVED";return true;
}
}
