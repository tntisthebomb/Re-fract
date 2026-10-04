#pragma once
#ifdef __3DS__
#include "engine.hpp"
#include <citro3d.h>
namespace rf {
class GpuSurface {
 bool initialized=false,ready=false;
 DVLB_s* shader=nullptr;shaderProgram_s program{};
 C3D_RenderTarget* targets[2]={nullptr,nullptr};
 SurfaceVertex* vertices=nullptr;uint16_t* indices=nullptr;
 int count=0,projectionLocation=-1;
 Vec origin;
 public:
 ~GpuSurface(){shutdown();}
 bool upload(const SurfaceMesh& mesh);
 bool draw(const Scene& scene,float slider);
 void synchronize();
 void shutdown();
 bool available()const{return ready&&count>0;}
 int triangles()const{return count/3;}
 float drawingMs()const{return initialized?C3D_GetDrawingTime():0;}
};
}
#endif
