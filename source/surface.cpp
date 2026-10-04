#include "engine.hpp"
#include <algorithm>
namespace rf {
std::array<float,16> meshProjection(const Scene& s,float eye,Vec relativeOrigin){
 Rays r(s);Vec origin=s.camera.position-relativeOrigin+r.right*eye;
 float horizontal=r.tangent*float(W)/H,near=s.settings.meshNear,far=s.settings.farClip;
 float a=near/(far-near),b=far*near/(far-near);
 Vec rows[]={r.up*(1/r.tangent),(r.right+r.forward*(eye/s.settings.convergence))*(-1/horizontal),r.forward*a,r.forward};
 std::array<float,16> out{};
 for(int i=0;i<4;++i){out[i*4]=rows[i].x;out[i*4+1]=rows[i].y;out[i*4+2]=rows[i].z;out[i*4+3]=-rows[i].dot(origin)-(i==2?b:0);}
 return out;
}
SurfaceMesh surfaceMesh(const Scene& s,const std::vector<Color>& colors,const std::vector<float>& depths,int block,float eye,ParallelFor loop,void* context){
 SurfaceMesh mesh;int stride=s.settings.meshStride;
 if(colors.size()!=W*H||depths.size()!=W*H||block<1||stride<4||stride>16||stride%block)return mesh;
 int nx=(W+stride-1)/stride,ny=(H+stride-1)/stride;
 mesh.origin=s.camera.position;mesh.vertices.resize(nx*ny);std::vector<float> distances(nx*ny,0);Rays rays(s);
 struct Work{const Scene& scene;const Rays& rays;const std::vector<Color>& colors;const std::vector<float>& depths;SurfaceMesh& mesh;std::vector<float>& distances;int nx,stride,block;float eye;};
 Work work{s,rays,colors,depths,mesh,distances,nx,stride,block,eye};
 auto row=[](int y,void* ctx){auto& w=*static_cast<Work*>(ctx);for(int x=0;x<w.nx;++x){int k=y*w.nx+x,src=y*w.stride*W+x*w.stride;float depth=w.depths[src];
   if(!std::isfinite(depth)||depth<=0||depth>=w.scene.settings.farClip)continue;
   Vec origin,direction;w.rays.ray(w.scene,x*w.stride+w.block*.5f,y*w.stride+w.block*.5f,w.eye,origin,direction);
   auto c=w.colors[src];w.mesh.vertices[k]={w.rays.right*w.eye+direction*depth,{c.r/255.f,c.g/255.f,c.b/255.f}};w.distances[k]=depth;
  }};
 if(loop&&s.settings.parallel)loop(ny,row,&work,context);else for(int y=0;y<ny;++y)row(y,&work);
 mesh.indices.reserve((nx-1)*(ny-1)*6);
 auto triangle=[&](int a,int b,int c){float da=distances[a],db=distances[b],dc=distances[c];if(da<=0||db<=0||dc<=0)return;
  float minimum=std::min(da,std::min(db,dc)),maximum=std::max(da,std::max(db,dc));
  // Reject silhouettes/disconnected depth layers rather than bridge empty space.
  if(maximum-minimum>minimum*s.settings.meshEdge+s.settings.epsilon*8)return;
  mesh.indices.push_back(uint16_t(a));mesh.indices.push_back(uint16_t(b));mesh.indices.push_back(uint16_t(c));
 };
 for(int y=0;y<ny-1;++y)for(int x=0;x<nx-1;++x){int a=y*nx+x,b=a+1,c=a+nx,d=c+1;triangle(a,c,b);triangle(b,c,d);}
 return mesh;
}
}
