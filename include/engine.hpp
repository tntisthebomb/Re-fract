#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>

namespace rf {
constexpr int W=400,H=240,MaxStages=12,MaxCode=128;
inline float clamp(float x,float a,float b){return std::fmax(a,std::fmin(b,x));}
struct Vec {
 float x=0,y=0,z=0;
 Vec()=default; Vec(float X,float Y,float Z):x(X),y(Y),z(Z){}
 Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
 Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
 Vec operator*(float s)const{return {x*s,y*s,z*s};}
 Vec multiply(Vec b)const{return {x*b.x,y*b.y,z*b.z};}
 float dot(Vec b)const{return x*b.x+y*b.y+z*b.z;}
 float length()const{return std::sqrt(dot(*this));}
 Vec unit()const{float n=length();return n>1e-12f?*this*(1/n):Vec{0,0,1};}
};
enum class Op {Constant,Variable,Add,Sub,Mul,Div,Pow,Neg,Sin,Cos,Abs,Sqrt,Log,Exp,Min,Max};
struct Instruction {Op op;float value=0;int variable=0;};
// Dual values carry partial derivatives w.r.t. x,y,z,cx,cy,cz.
struct Dual {float value=0;std::array<float,6> d{};};
struct Expression {
 std::array<Instruction,MaxCode> code{};int count=0,stack=0,native=0;
 bool compile(const std::string& text,std::string& error);
 bool evaluate(Vec z,Vec c,Dual& result)const;
 bool evaluateGeneric(Vec z,Vec c,Dual& result)const;
};
enum class Kind {BoxFold,SphereFold,Bulb,Scale,Rotate,Offset,Absolute,Sort,Menger,Tetra,Expression,KleinianFold,Inversion,Count};
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
 bool algebraicBulb=false;
 bool repeat=false;Vec repeatPeriod{16,16,16};
 float derivativeScale=1;
 int terminal=0;float terminalRadius=1;
 std::array<int,MaxStages> active{};int activeCount=0,kernel=0;
 bool validate(std::string& error);
};
Formula preset(int index);
const char* presetName(int index);
constexpr int PresetCount=40;
struct Sample {float distance=0,trap=0;bool valid=true;};
Sample distance(const Formula& f,Vec p,bool exact=false);
Sample distanceOnly(const Formula& f,Vec p,bool exact=false);
// Reference path for differential tests and host profiling.
Sample distanceGeneric(const Formula& f,Vec p,bool trap=true);
float repeatBoundaryStep(const Formula& f,Vec point,Vec direction);
struct PointLight {
 bool enabled=false,cameraRelative=true;
 Vec position{0,1,0},color{1,.85f,.65f};
 float intensity=5,range=8;
};
struct Settings {
 bool separateLighting=false,adaptiveLighting=false,depthPrepass=false,adaptiveRayBudget=false,rayBudgetRetry=true;
 int lightingMinPasses=4,lightingRefresh=8,prepassBlock=8,rayMinSteps=16;
 float lightingThreshold=.03f,lightingDepthTolerance=.08f,prepassSafety=.5f;
 bool adaptiveEmpty=false,adaptiveSkyEdges=false;int emptyProbe=8,skyEdgeProbe=2,skyNeighbors=5,emptyRefresh=4;
 int stillBlock=1,upscale=0;
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
 bool boundedGradient=true;int gradientStops=2;
 float emission=0,bloom=0;
 std::array<Vec,3> gradientMiddle{{{.3f,.2f,.6f},{.1f,.8f,.6f},{.95f,.6f,.1f}}};
 bool gpuCache=false;
 int meshStride=4;
 float meshEdge=.08f,meshNear=.005f;
 bool adaptivePrecision=false,gpuAutoRefresh=false;
 bool adaptiveDetail=false;
 bool distanceField=false;
 float fieldSpacing=.25f;
 int previewIterations=8,detailIterations=24;
 float minEpsilon=.000001f,pixelTolerance=.25f;
 int giSamples=0,giSteps=16;
 float giStrength=.5f,giRange=2;
 Vec skyColor{.15f,.2f,.3f};
 std::array<PointLight,2> pointLights;
 bool pointShadows=false,dof=false,progressiveLighting=false;
 float sunStrength=1,aperture=.03f,focusDistance=4;
 int dofSamples=4,lightingPasses=16,lightingBlock=4;
};
struct Camera {Vec position{0,0,-4};float yaw=0,pitch=0,speed=1;bool surfaceSpeed=true;float surfaceRange=1,minimumSpeed=.01f;};
Camera presetCamera(int index);
struct Scene {Formula formula=preset(0);Settings settings;Camera camera;};
Settings presetSettings(int index);
float hitTolerance(const Settings& settings,float distance,float tangent);
int renderingIterations(const Scene& scene,bool moving);
float cameraSpeedScale(const Scene& s);
Vec gradientColor(const Settings& s,float trap);
struct StereoSlider {
 float strength=0;bool active=false;
 bool update(const Settings& settings,float raw);
};
class DistanceField;
struct Rays {
 Vec forward,right,up,light;float tangent=1;
 const DistanceField* field=nullptr;
 std::array<Vec,2> pointPositions{};int pointMask=0;
 explicit Rays(const Scene& s);
 void ray(const Scene& s,float x,float y,float eye,Vec& origin,Vec& direction)const;
 void lensRay(const Scene& s,float x,float y,float eye,float lensX,float lensY,Vec& origin,Vec& direction)const;
 bool project(const Scene& s,Vec point,float eye,float& x,float& y,float& depth)const;
};
struct Color {uint8_t r=0,g=0,b=0;};
using LoopBody=void (*)(int,void*);
using ParallelFor=void (*)(int,LoopBody,void*,void*);
struct SurfaceVertex {Vec position,color;};
// Vertex positions are relative to origin to protect GPU float24 precision.
struct SurfaceMesh {Vec origin;std::vector<SurfaceVertex> vertices;std::vector<uint16_t> indices;};
// CPU-testable projection with the quarter-turn/depth range required by PICA.
std::array<float,16> meshProjection(const Scene& scene,float eye,Vec relativeOrigin={});
SurfaceMesh surfaceMesh(const Scene& scene,const std::vector<Color>& colors,const std::vector<float>& depths,int block,float eye,ParallelFor loop=nullptr,void* context=nullptr);
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye);
Color shadeCell(const Scene& s,const Rays& rays,int x,int y,int block,float eye,int count);
struct Profile {
 uint64_t rays=0,steps=0,distanceQueries=0,shadingQueries=0,reused=0,skipped=0,relaxFallbacks=0,batches=0,skySkipped=0,lightingSkipped=0,budgetRetries=0,depthStarts=0;
 void add(const Profile& p);
};
struct RenderJob {int x=0,y=0,block=1,eye=0;float offset=0;bool fast=false;bool moving=false;uint32_t sample=0;bool accumulate=false;float startDepth=0;bool skyLikely=false;const struct ShadeRecord* surface=nullptr;};
struct ShadeRecord {float trap=0,depth=0,lighting=0,specular=0;bool valid=false;Vec indirect{},localDiffuse{},localSpecular{};bool background=false,escaped=false;Vec normal{};};
Color recolorSample(const Settings& settings,const ShadeRecord& record);
struct RenderResult {ShadeRecord shade;Color color;Vec point;float depth=0;bool hit=false;Profile profile;Vec radiance{};};
RenderResult renderJob(const Scene& s,const Rays& rays,const RenderJob& job);
void renderJobs(const Scene& s,const Rays& rays,const RenderJob* jobs,RenderResult* results,int count);
using BatchShader=void (*)(const Scene&,const Rays&,const RenderJob*,RenderResult*,int,void*);
void upscaleImage(const std::vector<Color>& source,std::vector<Color>& output,int block,int method,ParallelFor loop=nullptr,void* context=nullptr);
class Renderer {
 std::array<std::vector<Color>,2> pixels,presented;bool presentedReady=false;
 void present(const Settings& settings,int eyes);
 std::array<std::vector<uint8_t>,2> emptyMask;int emptyBlock=0,emptyColumns=0,emptyRows=0,emptyPass=0;
 void classifyEmpty(const Settings& settings,int eyes);
 std::array<std::vector<float>,2> depths;
 std::array<std::vector<uint8_t>,2> ages;
 std::array<std::vector<ShadeRecord>,2> shades;
 std::array<std::vector<Vec>,2> accumulation;
 int lightingPass=0,finishedLightingPasses=0;
 bool lightingOnly=false;int geometryBlock=0,prepassSourceBlock=0;
 struct LightSample {Vec mean{},m2{};uint32_t count=0;};
 std::array<std::vector<LightSample>,2> lightSamples;
 std::array<std::vector<float>,2> prepassDepth;std::array<std::vector<uint8_t>,2> prepassSky;
 void resolveLighting(const Scene& scene,int eyes,const RenderJob* updated,int count);
 bool cacheReady=false;
 bool passTiming=false;std::chrono::steady_clock::time_point passStart;
 float lastPassMs=0,lastLiveMs=0;int lastPassBlock=0,lastLiveBlock=0,lastPassEyes=0;
 void finishPass(const Scene& scene,int eyes);void applyBloom(const Settings& settings,int eyes);
 struct History {Vec point;Color color;float footprint=0;uint32_t stamp=0;int eye=0;};
 std::array<History,8192> history{};size_t historyCount=0,historyCursor=0;
 uint32_t frame=0;int dynamicBlock=8,batchLimit=8,activeEyes=1;
 float eyeOffset=0;
 float averageJobMs=0,frameMs=0;Profile totals;
 int block=8,lane=0,index=0;bool moving=false,done=false;
 uint64_t rayCount=0,jobCount=0,revision=1;
 BatchShader batchShader=nullptr;void* shaderContext=nullptr;
 ParallelFor parallelFor=nullptr;void* loopContext=nullptr;
 void reproject(const Scene& s,const Rays& rays,float slider);
 bool skipCell(const Scene& s,int x,int y,int eye,int cell);
 public:
 Renderer();
 void setBatchShader(BatchShader shader,void* context){batchShader=shader;shaderContext=context;}
 void setParallelFor(ParallelFor loop,void* context){parallelFor=loop;loopContext=context;}
 void invalidate(const Scene& s,bool motion,bool clearHistory=true);
 void beginFrame(const Scene& s,bool motion,bool changed,float slider);
 bool recolor(const Scene& s);
 bool fitGradient(Scene& s)const;
 bool captureSurface(const Scene& s,SurfaceMesh& mesh)const;
 void endFrame(const Scene& s,float totalMs,float renderMs,uint64_t jobs);
 // A bounded batch can be split between cores in mono and stereo modes.
 void step(const Scene& s,const Rays& rays,float slider);
 const std::vector<Color>& image(int eye)const{return presentedReady?presented[activeEyes==1?0:eye]:pixels[activeEyes==1?0:eye];}
 bool complete()const{return done;}
 int currentBlock()const{return block;}
 uint64_t rays()const{return rayCount;}
 uint64_t jobs()const{return jobCount;}
 uint64_t imageRevision()const{return revision;}
 const Profile& profile()const{return totals;}
 void resetProfile(){totals={};}
 int previewSize()const{return dynamicBlock;}
 float measuredFrameMs()const{return frameMs;}
 float lastCompletedFrameMs()const{return lastPassMs;}
 float lastRealtimeFrameMs()const{return lastLiveMs;}
 int lastCompletedFrameBlock()const{return lastPassBlock;}
 int lastRealtimeFrameBlock()const{return lastLiveBlock;}
 int lastCompletedFrameEyes()const{return lastPassEyes;}
 int accumulatedPasses()const{return finishedLightingPasses;}
 float progress(const Scene& s)const;
};
bool saveScene(const Scene& scene,const std::string& path,std::string& error);
bool loadScene(Scene& scene,const std::string& path,std::string& error);
bool saveJPEG(const std::vector<Color>& pixels,int width,int height,const std::string& path,std::string& error);
bool saveMPO(const std::vector<Color>& left,const std::vector<Color>& right,const std::string& path,std::string& error);
bool saveBMP(const std::vector<Color>& pixels,int width,int height,const std::string& path,std::string& error);
bool savePPM(const std::vector<Color>& image,const std::string& path,std::string& error);
}
