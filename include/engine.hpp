#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace rf {
constexpr int W=400,H=240,MaxStages=12,MaxCode=128;
inline float clamp(float x,float a,float b){return std::fmax(a,std::fmin(b,x));}
struct Vec {
 float x=0,y=0,z=0;
 Vec()=default; Vec(float X,float Y,float Z):x(X),y(Y),z(Z){}
 Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
 Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
 Vec operator*(float s)const{return {x*s,y*s,z*s};}
 float dot(Vec b)const{return x*b.x+y*b.y+z*b.z;}
 float length()const{return std::sqrt(dot(*this));}
 Vec unit()const{float n=length();return n>1e-12f?*this*(1/n):Vec{0,0,1};}
};
enum class Op {Constant,Variable,Add,Sub,Mul,Div,Pow,Neg,Sin,Cos,Abs,Sqrt,Log,Exp,Min,Max};
struct Instruction {Op op;float value=0;int variable=0;};
// Dual values carry partial derivatives w.r.t. x,y,z,cx,cy,cz.
struct Dual {float value=0;std::array<float,6> d{};};
struct Expression {
 std::array<Instruction,MaxCode> code{};int count=0,stack=0;
 bool compile(const std::string& text,std::string& error);
 bool evaluate(Vec z,Vec c,Dual& result)const;
};
enum class Kind {BoxFold,SphereFold,Bulb,Scale,Rotate,Offset,Absolute,Sort,Menger,Tetra,Expression,Count};
const char* kindName(Kind k);
struct Stage {
 Kind kind=Kind::Scale;bool enabled=true;
 // Operation-specific controls. See docs/FORMULAS.md.
 float a=2,b=1,c=0;
 std::array<std::string,3> text{{"x","y","z"}};
 std::array<Expression,3> expr;
 std::array<float,9> rotation{{1,0,0,0,1,0,0,0,1}};
 float minimum2=.25f,fixed2=1,innerScale=4,thetaPower=0,phiPower=0,stretch=1;
 int integerPower=-1;
 bool compile(std::string& error);
};
struct Formula {
 std::vector<Stage> stages;int iterations=10;float bailout=8;
 bool logarithmic=false,julia=false;Vec constant{0,0,0};
 bool repeat=false;Vec repeatPeriod{16,16,16};
 float derivativeScale=1;
 int terminal=0;float terminalRadius=1;
 std::array<int,MaxStages> active{};int activeCount=0,kernel=0;
 bool validate(std::string& error);
};
Formula preset(int index);
const char* presetName(int index);
constexpr int PresetCount=18;
struct Sample {float distance=0,trap=0;bool valid=true;};
Sample distance(const Formula& f,Vec p);
Sample distanceOnly(const Formula& f,Vec p);
// Reference path for differential tests and host profiling.
Sample distanceGeneric(const Formula& f,Vec p,bool trap=true);
float repeatBoundaryStep(const Formula& f,Vec point,Vec direction);
struct Settings {
 int steps=64,previewBlock=8,interlace=4,ao=0,shadow=0,samples=1;
 float epsilon=.002f,safety=.65f,farClip=20,fov=55;
 float eyeSeparation=.065f,convergence=4,exposure=1,fog=.035f;
 float lightYaw=-.7f,lightPitch=.8f,palette=0,specular=.2f;
 bool stereo=true,quality=false,autoRefine=true,parallel=true;
 float budgetMs=8;
 int batchSize=8,targetFps=30,historyFrames=6,refreshRate=2;
 bool adaptiveResolution=true,previewLighting=true,temporal=false,stereoReuse=false;
 bool adaptiveTiles=false,foveated=false;
 float relaxation=1;
 bool customGradient=false,gradientRepeat=false;
 Vec gradientLow{.18f,.06f,.025f},gradientHigh{.95f,.58f,.22f};
 float gradientScale=.8f,gradientOffset=0;
};
struct Camera {Vec position{0,0,-4};float yaw=0,pitch=0,speed=1;bool surfaceSpeed=true;float surfaceRange=1,minimumSpeed=.01f;};
Camera presetCamera(int index);
struct Scene {Formula formula=preset(0);Settings settings;Camera camera;};
float cameraSpeedScale(const Scene& s);
Vec gradientColor(const Settings& s,float trap);
struct StereoSlider {
 float strength=0;bool active=false;
 bool update(const Settings& settings,float raw);
};
struct Rays {
 Vec forward,right,up,light;float tangent=1;
 explicit Rays(const Scene& s);
 void ray(const Scene& s,float x,float y,float eye,Vec& origin,Vec& direction)const;
 bool project(const Scene& s,Vec point,float eye,float& x,float& y,float& depth)const;
};
struct Color {uint8_t r=0,g=0,b=0;};
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye);
Color shadeCell(const Scene& s,const Rays& rays,int x,int y,int block,float eye,int count);
struct Profile {
 uint64_t rays=0,steps=0,distanceQueries=0,shadingQueries=0,reused=0,skipped=0,relaxFallbacks=0,batches=0;
 void add(const Profile& p);
};
struct RenderJob {int x=0,y=0,block=1,eye=0;float offset=0;bool fast=false;};
struct RenderResult {Color color;Vec point;float depth=0;bool hit=false;Profile profile;};
RenderResult renderJob(const Scene& s,const Rays& rays,const RenderJob& job);
void renderJobs(const Scene& s,const Rays& rays,const RenderJob* jobs,RenderResult* results,int count);
using BatchShader=void (*)(const Scene&,const Rays&,const RenderJob*,RenderResult*,int,void*);
class Renderer {
 std::array<std::vector<Color>,2> pixels;
 std::array<std::vector<float>,2> depths;
 std::array<std::vector<uint8_t>,2> ages;
 struct History {Vec point;Color color;float footprint=0;uint32_t stamp=0;int eye=0;};
 std::array<History,8192> history{};size_t historyCount=0,historyCursor=0;
 uint32_t frame=0;int dynamicBlock=8,batchLimit=8,activeEyes=1;
 float eyeOffset=0;
 float averageJobMs=0,frameMs=0;Profile totals;
 int block=8,lane=0,index=0;bool moving=false,done=false;
 uint64_t rayCount=0,revision=1;
 BatchShader batchShader=nullptr;void* shaderContext=nullptr;
 void reproject(const Scene& s,const Rays& rays,float slider);
 bool skipCell(const Scene& s,int x,int y,int eye,int cell);
 public:
 Renderer();
 void setBatchShader(BatchShader shader,void* context){batchShader=shader;shaderContext=context;}
 void invalidate(const Scene& s,bool motion,bool clearHistory=true);
 void beginFrame(const Scene& s,bool motion,bool changed,float slider);
 void endFrame(const Scene& s,float totalMs,float renderMs,uint64_t jobs);
 // A bounded batch can be split between cores in mono and stereo modes.
 void step(const Scene& s,const Rays& rays,float slider);
 const std::vector<Color>& image(int eye)const{return pixels[activeEyes==1?0:eye];}
 bool complete()const{return done;}
 int currentBlock()const{return block;}
 uint64_t rays()const{return rayCount;}
 uint64_t imageRevision()const{return revision;}
 const Profile& profile()const{return totals;}
 void resetProfile(){totals={};}
 int previewSize()const{return dynamicBlock;}
 float measuredFrameMs()const{return frameMs;}
 float progress(const Scene& s)const;
};
bool saveScene(const Scene& scene,const std::string& path,std::string& error);
bool loadScene(Scene& scene,const std::string& path,std::string& error);
bool savePPM(const std::vector<Color>& image,const std::string& path,std::string& error);
}
