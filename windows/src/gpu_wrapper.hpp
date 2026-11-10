#pragma once
// Optional GPU backend. Built only when gpu_renderer.cpp is present (RF_HAVE_GPU); otherwise every call reports "unavailable".
#include "engine.hpp"
#ifdef RF_HAVE_GPU
#include "gpu_renderer.hpp"
#endif
namespace rf {
class Gpu {
#ifdef RF_HAVE_GPU
 GpuRenderer r;bool up=false;
 public:
 bool init(std::string& e){if(up)return true;up=r.init(e);return up;}
 bool ready()const{return up;}
 void shutdown(){if(up)r.shutdown();up=false;}
 bool resize(int w,int h){return up&&r.resize(w,h);}
 bool begin(const Scene& s,float o,std::string& e){return up&&r.begin(s,o,e);}
 bool render(const Scene& s,int w,int h,float ms){return up&&r.render(s,w,h,ms);}
 int eyes()const{return up?r.eyes():1;}
 unsigned texture(int e)const{return up?r.texture(e):0;}
 bool readback(int e,std::vector<Color>& o,int& w,int& h){return up&&r.readback(e,o,w,h);}
 const char* name()const{return up?r.rendererName():"none";}
#else
 public:
 bool init(std::string& e){e="built without GPU renderer";return false;}
 bool ready()const{return false;}
 void shutdown(){}
 bool resize(int,int){return false;}
 bool begin(const Scene&,float,std::string&){return false;}
 bool render(const Scene&,int,int,float){return false;}
 int eyes()const{return 1;}
 unsigned texture(int)const{return 0;}
 bool readback(int,std::vector<Color>&,int&,int&){return false;}
 const char* name()const{return "none";}
#endif
};
}
