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
 bool compile(std::string& error);
};
struct Formula {
 std::vector<Stage> stages;int iterations=10;float bailout=8;
 bool logarithmic=false,julia=false;Vec constant{0,0,0};
 float derivativeScale=1;
 int terminal=0;float terminalRadius=1;
 bool validate(std::string& error);
};
Formula preset(int index);
const char* presetName(int index);
constexpr int PresetCount=18;
struct Sample {float distance=0,trap=0;bool valid=true;};
Sample distance(const Formula& f,Vec p);
struct Settings {
 int steps=64,previewBlock=8,interlace=4,ao=0,shadow=0,samples=1;
 float epsilon=.002f,safety=.65f,farClip=20,fov=55;
 float eyeSeparation=.065f,convergence=4,exposure=1,fog=.035f;
 float lightYaw=-.7f,lightPitch=.8f,palette=0,specular=.2f;
 bool stereo=true,quality=false,autoRefine=true,parallel=true;
 float budgetMs=8;
};
struct Camera {Vec position{0,0,-4};float yaw=0,pitch=0,speed=1;};
Camera presetCamera(int index);
struct Scene {Formula formula=preset(0);Settings settings;Camera camera;};
struct Rays {
 Vec forward,right,up,light;float tangent=1;
 explicit Rays(const Scene& s);
 void ray(const Scene& s,float x,float y,float eye,Vec& origin,Vec& direction)const;
};
struct Color {uint8_t r=0,g=0,b=0;};
Color trace(const Scene& s,const Rays& rays,float x,float y,float eye);
Color shadeCell(const Scene& s,const Rays& rays,int x,int y,int block,float eye,int count);
using StereoShader=void (*)(const Scene&,const Rays&,int,int,int,float,int,Color&,Color&,void*);
class Renderer {
 std::array<std::vector<Color>,2> pixels;
 int block=8,lane=0,index=0;bool moving=false,done=false;
 uint64_t rayCount=0;
 StereoShader stereoShader=nullptr;void* shaderContext=nullptr;
 public:
 Renderer();
 void setStereoShader(StereoShader shader,void* context){stereoShader=shader;shaderContext=context;}
 void invalidate(const Scene& s,bool motion);
 // One sample location, both eyes together; fixed-size work avoids a whole-frame stall.
 void step(const Scene& s,const Rays& rays,float slider);
 const std::vector<Color>& image(int eye)const{return pixels[eye];}
 bool complete()const{return done;}
 int currentBlock()const{return block;}
 uint64_t rays()const{return rayCount;}
 float progress(const Scene& s)const;
};
bool saveScene(const Scene& scene,const std::string& path,std::string& error);
bool loadScene(Scene& scene,const std::string& path,std::string& error);
bool savePPM(const std::vector<Color>& image,const std::string& path,std::string& error);
}
