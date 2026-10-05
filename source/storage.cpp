#include "engine.hpp"
#include <fstream>
#include <iomanip>
#include <cstdio>

namespace rf {
namespace {
bool settingsValid(const Scene& s){
 const Settings& v=s.settings;
 auto range=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
 if((v.lightingBlock!=1&&v.lightingBlock!=2&&v.lightingBlock!=4&&v.lightingBlock!=8&&v.lightingBlock!=16)||!range(v.emission,0,100)||!range(v.bloom,0,100)||v.gradientStops<2||v.gradientStops>5)return false;
 for(Vec color:v.gradientMiddle)if(!range(color.x,0,1)||!range(color.y,0,1)||!range(color.z,0,1))return false;
 for(const auto& lamp:v.pointLights)if(!range(lamp.position.x,-10000,10000)||!range(lamp.position.y,-10000,10000)||!range(lamp.position.z,-10000,10000)||
  !range(lamp.color.x,0,1)||!range(lamp.color.y,0,1)||!range(lamp.color.z,0,1)||!range(lamp.intensity,0,10000)||!range(lamp.range,.0001f,10000))return false;
 return range(v.sunStrength,0,100)&&range(v.aperture,0,100)&&range(v.focusDistance,.0001f,10000)&&v.lightingPasses>=1&&v.lightingPasses<=4096&&
 (v.dofSamples==1||v.dofSamples==2||v.dofSamples==4||v.dofSamples==8||v.dofSamples==16)&&range(v.fieldSpacing,.05f,1)&&v.previewIterations>=1&&v.previewIterations<=256&&v.detailIterations>=1&&v.detailIterations<=256&&v.giSamples>=0&&v.giSamples<=64&&v.giSteps>=4&&v.giSteps<=4096&&range(v.giStrength,0,20)&&range(v.giRange,.0001f,10000)&&
 range(v.minEpsilon,1e-9f,1)&&range(v.pixelTolerance,.05f,2)&&
 range(v.skyColor.x,0,1)&&range(v.skyColor.y,0,1)&&range(v.skyColor.z,0,1)&&v.steps>=8&&v.steps<=4096&&(v.previewBlock==4||v.previewBlock==8||v.previewBlock==16)&&v.interlace>=1&&v.interlace<=8&&
 v.ao>=0&&v.ao<=64&&v.shadow>=0&&v.shadow<=4096&&(v.samples==1||v.samples==2||v.samples==4)&&
 range(v.epsilon,1e-9f,1)&&range(v.safety,.001f,4)&&range(v.farClip,.01f,10000)&&range(v.fov,1,175)&&
 range(v.eyeSeparation,0,.2f)&&range(v.convergence,.0001f,10000)&&range(v.exposure,0,100)&&range(v.fog,0,100)&&
 range(v.lightYaw,-6.3f,6.3f)&&range(v.lightPitch,-1.5f,1.5f)&&range(v.palette,0,3)&&range(v.specular,0,20)&&range(v.budgetMs,1,100)&&
 range(s.camera.position.x,-10000,10000)&&range(s.camera.position.y,-10000,10000)&&range(s.camera.position.z,-10000,10000)&&
 v.batchSize>=1&&v.batchSize<=32&&v.targetFps>=15&&v.targetFps<=60&&v.historyFrames>=1&&v.historyFrames<=30&&v.refreshRate>=1&&v.refreshRate<=8&&
 range(v.relaxation,1,1.5f)&&range(v.gradientScale,-10000,10000)&&range(v.gradientOffset,-10000,10000)&&
 (v.meshStride==4||v.meshStride==8||v.meshStride==16)&&range(v.meshEdge,.005f,.5f)&&range(v.meshNear,.0001f,.5f)&&
 range(v.gradientLow.x,0,1)&&range(v.gradientLow.y,0,1)&&range(v.gradientLow.z,0,1)&&range(v.gradientHigh.x,0,1)&&range(v.gradientHigh.y,0,1)&&range(v.gradientHigh.z,0,1)&&
 range(s.camera.surfaceRange,.001f,20)&&range(s.camera.minimumSpeed,.0001f,1)&&range(s.camera.yaw,-6.3f,6.3f)&&range(s.camera.pitch,-1.5f,1.5f)&&range(s.camera.speed,.00001f,1000);
}
}
bool saveScene(const Scene& s,const std::string& path,std::string& error){
 Scene checked=s;if(!checked.formula.validate(error)||!settingsValid(s)){if(error.empty())error="Invalid scene settings";return false;}
 std::ofstream out(path+".tmp");if(!out){error="Cannot write scene";return false;}
 out<<std::setprecision(9)<<"REFRACT 7\n";
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
 out<<"GRADIENT "<<v.boundedGradient<<' '<<v.gradientStops<<' '<<v.emission<<' '<<v.bloom<<' '<<v.lightingBlock;
 for(Vec color:v.gradientMiddle)out<<' '<<color.x<<' '<<color.y<<' '<<color.z;
 out<<'\n';
 out.close();if(!out){error="Scene write failed";std::remove((path+".tmp").c_str());return false;}
 if(std::rename((path+".tmp").c_str(),path.c_str())!=0){error="Cannot replace scene";return false;}
 error="SCENE SAVED";return true;
}
bool loadScene(Scene& s,const std::string& path,std::string& error){
 std::ifstream in(path,std::ios::binary);if(!in){error="Scene slot is empty";return false;}
 in.seekg(0,std::ios::end);auto size=in.tellg();if(size<0||size>16384){error="Scene file too large";return false;}in.seekg(0);
 Scene next;next.settings.boundedGradient=false;std::string magic;int version=0;in>>magic>>version;
 if(magic!="REFRACT"||(version<1||version>7)){error="Unsupported scene format";return false;}
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
 if(version>=7){std::string tag;in>>tag>>v.boundedGradient>>v.gradientStops>>v.emission>>v.bloom>>v.lightingBlock;for(Vec& color:v.gradientMiddle)in>>color.x>>color.y>>color.z;if(tag!="GRADIENT"){error="Missing gradient settings";return false;}}
 if(!in){error="Truncated or malformed scene";return false;}in>>std::ws;if(!in.eof()){error="Unexpected trailing data";return false;}
 if(!settingsValid(next)){error="Settings outside supported range";return false;}
 if(!f.validate(error))return false;
 s=next;error="SCENE LOADED";return true;
}
bool saveBMP(const std::vector<Color>& pixels,int width,int height,const std::string& path,std::string& error){
 if(width<=0||height<=0||width>4096||height>4096||pixels.size()!=size_t(width)*height){error="Invalid screenshot dimensions";return false;}
 uint32_t stride=(uint32_t(width)*3+3)&~3u,size=54+stride*height;std::ofstream out(path+".tmp",std::ios::binary);if(!out){error="Cannot write screenshot";return false;}
 auto word=[&](uint32_t n,int bytes){for(int i=0;i<bytes;++i)out.put(char((n>>(8*i))&255));};
 out.write("BM",2);word(size,4);word(0,4);word(54,4);word(40,4);word(width,4);word(height,4);word(1,2);word(24,2);word(0,4);word(stride*height,4);word(2835,4);word(2835,4);word(0,4);word(0,4);
 for(int y=height-1;y>=0;--y){for(int x=0;x<width;++x){Color c=pixels[y*width+x];out.put(char(c.b));out.put(char(c.g));out.put(char(c.r));}for(uint32_t pad=width*3;pad<stride;++pad)out.put(0);}
 out.close();if(!out){std::remove((path+".tmp").c_str());error="Screenshot write failed";return false;}
 if(std::rename((path+".tmp").c_str(),path.c_str())!=0){std::remove((path+".tmp").c_str());error="Cannot finish screenshot";return false;}
 error="SCREENSHOT SAVED";return true;
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
