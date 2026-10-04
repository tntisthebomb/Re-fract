#include "engine.hpp"
#include <fstream>
#include <iomanip>
#include <cstdio>

namespace rf {
namespace {
bool settingsValid(const Scene& s){
 const Settings& v=s.settings;
 auto range=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
 for(const auto& lamp:v.pointLights)if(!range(lamp.position.x,-10000,10000)||!range(lamp.position.y,-10000,10000)||!range(lamp.position.z,-10000,10000)||
  !range(lamp.color.x,0,1)||!range(lamp.color.y,0,1)||!range(lamp.color.z,0,1)||!range(lamp.intensity,0,50)||!range(lamp.range,.1f,100))return false;
 return range(v.sunStrength,0,4)&&range(v.aperture,0,1)&&range(v.focusDistance,.001f,100)&&v.lightingPasses>=1&&v.lightingPasses<=128&&
 (v.dofSamples==1||v.dofSamples==2||v.dofSamples==4||v.dofSamples==8||v.dofSamples==16)&&range(v.fieldSpacing,.05f,1)&&v.previewIterations>=1&&v.previewIterations<=32&&v.detailIterations>=1&&v.detailIterations<=32&&v.giSamples>=0&&v.giSamples<=4&&v.giSteps>=4&&v.giSteps<=64&&range(v.giStrength,0,2)&&range(v.giRange,.1f,10)&&
 range(v.minEpsilon,.000001f,.001f)&&range(v.pixelTolerance,.05f,2)&&
 range(v.skyColor.x,0,1)&&range(v.skyColor.y,0,1)&&range(v.skyColor.z,0,1)&&v.steps>=8&&v.steps<=256&&(v.previewBlock==4||v.previewBlock==8||v.previewBlock==16)&&v.interlace>=1&&v.interlace<=8&&
 v.ao>=0&&v.ao<=6&&v.shadow>=0&&v.shadow<=64&&(v.samples==1||v.samples==2||v.samples==4)&&
 range(v.epsilon,.000001f,.05f)&&range(v.safety,.05f,1)&&range(v.farClip,1,100)&&range(v.fov,20,100)&&
 range(v.eyeSeparation,0,.2f)&&range(v.convergence,.2f,30)&&range(v.exposure,.1f,4)&&range(v.fog,0,1)&&
 range(v.lightYaw,-6.3f,6.3f)&&range(v.lightPitch,-1.5f,1.5f)&&range(v.palette,0,3)&&range(v.specular,0,1)&&range(v.budgetMs,1,20)&&
 range(s.camera.position.x,-10000,10000)&&range(s.camera.position.y,-10000,10000)&&range(s.camera.position.z,-10000,10000)&&
 v.batchSize>=1&&v.batchSize<=32&&v.targetFps>=15&&v.targetFps<=60&&v.historyFrames>=1&&v.historyFrames<=30&&v.refreshRate>=1&&v.refreshRate<=8&&
 range(v.relaxation,1,1.5f)&&range(v.gradientScale,.01f,100)&&range(v.gradientOffset,-100,100)&&
 (v.meshStride==4||v.meshStride==8||v.meshStride==16)&&range(v.meshEdge,.005f,.5f)&&range(v.meshNear,.0001f,.5f)&&
 range(v.gradientLow.x,0,1)&&range(v.gradientLow.y,0,1)&&range(v.gradientLow.z,0,1)&&range(v.gradientHigh.x,0,1)&&range(v.gradientHigh.y,0,1)&&range(v.gradientHigh.z,0,1)&&
 range(s.camera.surfaceRange,.001f,20)&&range(s.camera.minimumSpeed,.0001f,1)&&range(s.camera.yaw,-6.3f,6.3f)&&range(s.camera.pitch,-1.5f,1.5f)&&range(s.camera.speed,.01f,10);
}
}
bool saveScene(const Scene& s,const std::string& path,std::string& error){
 Scene checked=s;if(!checked.formula.validate(error)||!settingsValid(s)){if(error.empty())error="Invalid scene settings";return false;}
 std::ofstream out(path+".tmp");if(!out){error="Cannot write scene";return false;}
 out<<std::setprecision(9)<<"REFRACT 6\n";
 const Formula& f=s.formula;const Settings& v=s.settings;const Camera& c=s.camera;
 out<<f.iterations<<' '<<f.bailout<<' '<<f.logarithmic<<' '<<f.julia<<' '<<f.constant.x<<' '<<f.constant.y<<' '<<f.constant.z<<' '<<f.derivativeScale<<' '<<f.terminal<<' '<<f.terminalRadius<<'\n';
 out<<f.repeat<<' '<<f.repeatPeriod.x<<' '<<f.repeatPeriod.y<<' '<<f.repeatPeriod.z<<'\n';
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
 out<<"GPU "<<v.gpuCache<<' '<<v.meshStride<<' '<<v.meshEdge<<' '<<v.meshNear<<' '<<f.algebraicBulb<<'\n';
 out<<"LIGHT "<<v.adaptivePrecision<<' '<<v.minEpsilon<<' '<<v.pixelTolerance<<' '<<v.gpuAutoRefresh<<' '
 <<v.giSamples<<' '<<v.giSteps<<' '<<v.giStrength<<' '<<v.giRange<<' '<<v.skyColor.x<<' '<<v.skyColor.y<<' '<<v.skyColor.z<<' '<<v.adaptiveDetail<<' '<<v.previewIterations<<' '<<v.detailIterations<<' '<<v.distanceField<<' '<<v.fieldSpacing<<'\n';
 out<<"OPTICS "<<v.pointShadows<<' '<<v.sunStrength<<' '<<v.dof<<' '<<v.aperture<<' '<<v.focusDistance<<' '<<v.dofSamples<<' '<<v.progressiveLighting<<' '<<v.lightingPasses<<'\n';
 for(const auto& lamp:v.pointLights)out<<"POINT "<<lamp.enabled<<' '<<lamp.cameraRelative<<' '<<lamp.position.x<<' '<<lamp.position.y<<' '<<lamp.position.z<<' '
  <<lamp.color.x<<' '<<lamp.color.y<<' '<<lamp.color.z<<' '<<lamp.intensity<<' '<<lamp.range<<'\n';
 out.close();if(!out){error="Scene write failed";std::remove((path+".tmp").c_str());return false;}
 if(std::rename((path+".tmp").c_str(),path.c_str())!=0){error="Cannot replace scene";return false;}
 error="SCENE SAVED";return true;
}
bool loadScene(Scene& s,const std::string& path,std::string& error){
 std::ifstream in(path,std::ios::binary);if(!in){error="Scene slot is empty";return false;}
 in.seekg(0,std::ios::end);auto size=in.tellg();if(size<0||size>16384){error="Scene file too large";return false;}in.seekg(0);
 Scene next;std::string magic;int version=0;in>>magic>>version;
 if(magic!="REFRACT"||(version<1||version>6)){error="Unsupported scene format";return false;}
 Formula& f=next.formula;Settings& v=next.settings;Camera& c=next.camera;
 in>>f.iterations>>f.bailout>>f.logarithmic>>f.julia>>f.constant.x>>f.constant.y>>f.constant.z>>f.derivativeScale>>f.terminal>>f.terminalRadius;
 if(version>=3)in>>f.repeat>>f.repeatPeriod.x>>f.repeatPeriod.y>>f.repeatPeriod.z;
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
 if(version>=4){std::string extension;in>>extension>>v.gpuCache>>v.meshStride>>v.meshEdge>>v.meshNear>>f.algebraicBulb;if(extension!="GPU"){error="Missing GPU settings";return false;}}
 if(version>=5){std::string extension;in>>extension>>v.adaptivePrecision>>v.minEpsilon>>v.pixelTolerance>>v.gpuAutoRefresh
 >>v.giSamples>>v.giSteps>>v.giStrength>>v.giRange>>v.skyColor.x>>v.skyColor.y>>v.skyColor.z>>v.adaptiveDetail>>v.previewIterations>>v.detailIterations>>v.distanceField>>v.fieldSpacing;
 if(extension!="LIGHT"){error="Missing lighting settings";return false;}}
 if(version>=6){std::string tag;in>>tag>>v.pointShadows>>v.sunStrength>>v.dof>>v.aperture>>v.focusDistance>>v.dofSamples>>v.progressiveLighting>>v.lightingPasses;
  if(tag!="OPTICS"){error="Missing optics settings";return false;}
  for(auto& lamp:v.pointLights){in>>tag>>lamp.enabled>>lamp.cameraRelative>>lamp.position.x>>lamp.position.y>>lamp.position.z
   >>lamp.color.x>>lamp.color.y>>lamp.color.z>>lamp.intensity>>lamp.range;if(tag!="POINT"){error="Missing point light";return false;}}
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
 static_assert(sizeof(Color)==3,"PPM export needs packed RGB bytes");
 out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*sizeof(Color));
 out.close();if(!out){error="Image write failed";return false;}error="IMAGE SAVED";return true;
}
}
