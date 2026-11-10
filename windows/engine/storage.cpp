#include "engine.hpp"
#include <fstream>
#include <iomanip>
#include <cstdio>

namespace rf {
namespace {
bool settingsValid(const Scene& s){
 // No artificial parameter limits: values only have to be finite and satisfy what the math needs (positive divisors,
 // valid enums, integer counts >= their minimum). Block sizes are capped at 2^20 only to keep index arithmetic in int range.
 const Settings& v=s.settings;
 auto fin=[](float x){return std::isfinite(x);};
 auto pos=[](float x){return std::isfinite(x)&&x>0;};
 auto nonneg=[](float x){return std::isfinite(x)&&x>=0;};
 auto unit=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
 auto block=[](int b){return b>=1&&b<=(1<<20);};
 auto color=[&](Vec c){return unit(c.x)&&unit(c.y)&&unit(c.z);};
 if(v.lightingMinPasses<1||v.lightingRefresh<0||v.rayMinSteps<1||!block(v.prepassBlock)||!pos(v.lightingThreshold)||!pos(v.lightingDepthTolerance)||!fin(v.prepassSafety)||v.prepassSafety<0||v.prepassSafety>=1)return false;
 if(v.skyEdgeProbe<1||v.skyNeighbors<1||v.emptyRefresh<0||v.emptyProbe<1||!block(v.stillBlock)||v.upscale<0||v.upscale>2||!block(v.lightingBlock)||!nonneg(v.emission)||!nonneg(v.bloom)||v.gradientStops<2||v.gradientStops>5)return false;
 for(Vec c:v.gradientMiddle)if(!color(c))return false;
 for(const auto& lamp:v.pointLights)if(!fin(lamp.position.x)||!fin(lamp.position.y)||!fin(lamp.position.z)||!color(lamp.color)||!nonneg(lamp.intensity)||!pos(lamp.range))return false;
 return nonneg(v.sunStrength)&&nonneg(v.aperture)&&pos(v.focusDistance)&&v.lightingPasses>=1&&v.dofSamples>=1&&pos(v.fieldSpacing)&&v.previewIterations>=1&&v.detailIterations>=1&&v.giSamples>=0&&v.giSteps>=1&&nonneg(v.giStrength)&&pos(v.giRange)&&
 pos(v.minEpsilon)&&pos(v.pixelTolerance)&&color(v.skyColor)&&v.steps>=1&&block(v.previewBlock)&&v.interlace>=1&&
 v.ao>=0&&v.shadow>=0&&v.samples>=1&&v.refineSamples>=1&&fin(v.fogScatter)&&fin(v.fogAmbient)&&fin(v.fogAnisotropy)&&v.fogAnisotropy>-1&&v.fogAnisotropy<1&&v.fogSteps>=1&&v.fogShadowSteps>=0&&fin(v.fogColor.x)&&fin(v.fogColor.y)&&fin(v.fogColor.z)&&
 pos(v.epsilon)&&pos(v.safety)&&pos(v.farClip)&&fin(v.fov)&&v.fov>0&&v.fov<180&&
 nonneg(v.eyeSeparation)&&pos(v.convergence)&&nonneg(v.exposure)&&nonneg(v.fog)&&
 fin(v.lightYaw)&&fin(v.lightPitch)&&fin(v.palette)&&v.palette>=0&&v.palette<=3&&nonneg(v.specular)&&pos(v.budgetMs)&&
 fin(s.camera.position.x)&&fin(s.camera.position.y)&&fin(s.camera.position.z)&&
 v.batchSize>=1&&v.targetFps>=1&&v.historyFrames>=1&&v.refreshRate>=1&&
 (fin(v.relaxation)&&v.relaxation>=1)&&fin(v.gradientScale)&&fin(v.gradientOffset)&&
 (v.meshStride==4||v.meshStride==8||v.meshStride==16)&&pos(v.meshEdge)&&pos(v.meshNear)&&
 color(v.gradientLow)&&color(v.gradientHigh)&&
 pos(s.camera.surfaceRange)&&pos(s.camera.minimumSpeed)&&fin(s.camera.yaw)&&fin(s.camera.pitch)&&std::fabs(s.camera.pitch)<=1.5707f&&pos(s.camera.speed);
}
}
bool saveScene(const Scene& s,const std::string& path,std::string& error){
 Scene checked=s;if(!checked.formula.validate(error)||!settingsValid(s)){if(error.empty())error="Invalid scene settings";return false;}
 std::ofstream out(path+".tmp");if(!out){error="Cannot write scene";return false;}
 out<<std::setprecision(9)<<"REFRACT 12\n";
 const Formula& f=s.formula;const Settings& v=s.settings;const Camera& c=s.camera;
 out<<f.iterations<<' '<<f.bailout<<' '<<f.logarithmic<<' '<<f.julia<<' '<<f.constant.x<<' '<<f.constant.y<<' '<<f.constant.z<<' '<<f.derivativeScale<<' '<<f.terminal<<' '<<f.terminalRadius<<'\n';
 out<<f.repeat<<' '<<f.repeatPeriod.x<<' '<<f.repeatPeriod.y<<' '<<f.repeatPeriod.z<<'\n';
 out<<v.steps<<' '<<v.previewBlock<<' '<<v.interlace<<' '<<v.ao<<' '<<v.shadow<<' '<<v.samples<<' '
 <<v.epsilon<<' '<<v.safety<<' '<<v.farClip<<' '<<v.fov<<' '<<v.eyeSeparation<<' '<<v.convergence<<' '
 <<v.exposure<<' '<<v.fog<<' '<<v.lightYaw<<' '<<v.lightPitch<<' '<<v.palette<<' '<<v.specular<<' '
 <<v.stereo<<' '<<v.quality<<' '<<v.autoRefine<<' '<<v.parallel<<' '<<v.budgetMs<<'\n';
 out<<std::setprecision(17)<<c.position.x<<' '<<c.position.y<<' '<<c.position.z<<std::setprecision(9)<<' '<<c.yaw<<' '<<c.pitch<<' '<<c.speed<<'\n';
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
 out<<"DISPLAY "<<v.stillBlock<<' '<<v.upscale<<'\n';
 out<<"EMPTY "<<v.adaptiveEmpty<<' '<<v.emptyProbe<<' '<<v.adaptiveSkyEdges<<' '<<v.skyEdgeProbe<<' '<<v.skyNeighbors<<' '<<v.emptyRefresh<<'\n';
 out<<"PERF "<<v.separateLighting<<' '<<v.adaptiveLighting<<' '<<v.depthPrepass<<' '<<v.adaptiveRayBudget<<' '<<v.rayBudgetRetry<<' '<<v.lightingMinPasses<<' '<<v.lightingRefresh<<' '<<v.prepassBlock<<' '<<v.rayMinSteps<<' '<<v.lightingThreshold<<' '<<v.lightingDepthTolerance<<' '<<v.prepassSafety<<'\n';
 out<<"REFINE "<<v.refineSamples<<'\n';
 out<<"FOG "<<v.volumetricFog<<' '<<v.fogScatter<<' '<<v.fogAnisotropy<<' '<<v.fogAmbient<<' '<<v.fogSteps<<' '<<v.fogShadowSteps<<' '<<v.fogColor.x<<' '<<v.fogColor.y<<' '<<v.fogColor.z<<'\n';
 out.close();if(!out){error="Scene write failed";std::remove((path+".tmp").c_str());return false;}
 if(std::rename((path+".tmp").c_str(),path.c_str())!=0){error="Cannot replace scene";return false;}
 error="SCENE SAVED";return true;
}
bool loadScene(Scene& s,const std::string& path,std::string& error){
 std::ifstream in(path,std::ios::binary);if(!in){error="Scene slot is empty";return false;}
 in.seekg(0,std::ios::end);auto size=in.tellg();if(size<0||size>(64<<20)){error="Scene file too large";return false;}in.seekg(0);
 Scene next;next.settings.boundedGradient=false;std::string magic;int version=0;in>>magic>>version;
 if(magic!="REFRACT"||(version<1||version>12)){error="Unsupported scene format";return false;}
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
 int count=0;in>>count;if(count<1||count>1000000){error="Invalid stage count";return false;}
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
 if(version>=8){std::string tag;in>>tag>>v.stillBlock>>v.upscale;if(tag!="DISPLAY"){error="Missing display settings";return false;}}
 if(version>=9){std::string tag;in>>tag>>v.adaptiveEmpty>>v.emptyProbe>>v.adaptiveSkyEdges>>v.skyEdgeProbe>>v.skyNeighbors>>v.emptyRefresh;if(tag!="EMPTY"){error="Missing empty-space settings";return false;}}
 if(version>=10){std::string tag;in>>tag>>v.separateLighting>>v.adaptiveLighting>>v.depthPrepass>>v.adaptiveRayBudget>>v.rayBudgetRetry>>v.lightingMinPasses>>v.lightingRefresh>>v.prepassBlock>>v.rayMinSteps>>v.lightingThreshold>>v.lightingDepthTolerance>>v.prepassSafety;if(tag!="PERF"){error="Missing performance settings";return false;}}
 if(version>=11){std::string tag;in>>tag>>v.refineSamples;if(tag!="REFINE"){error="Missing refine settings";return false;}}
 if(version>=12){std::string tag;in>>tag>>v.volumetricFog>>v.fogScatter>>v.fogAnisotropy>>v.fogAmbient>>v.fogSteps>>v.fogShadowSteps>>v.fogColor.x>>v.fogColor.y>>v.fogColor.z;if(tag!="FOG"){error="Missing fog settings";return false;}}
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
