#include "engine.hpp"
#include <algorithm>
namespace rf {
namespace {
using Axis=UpscaleAxis;
Axis axis(float coordinate,int size,int method){
 int i=int(std::floor(coordinate));float t=coordinate-i;Axis a{};
 if(method==1){a.index[0]=std::max(0,std::min(size-1,i));a.index[1]=std::max(0,std::min(size-1,i+1));a.weight[0]=1-t;a.weight[1]=t;}
 else{float t2=t*t,t3=t2*t;a.weight[0]=-.5f*t+t2-.5f*t3;a.weight[1]=1-2.5f*t2+1.5f*t3;a.weight[2]=.5f*t+2*t2-1.5f*t3;a.weight[3]=-.5f*t2+.5f*t3;for(int j=0;j<4;++j)a.index[j]=std::max(0,std::min(size-1,i+j-1));}
 return a;
}
}
void upscaleImage(const std::vector<Color>& source,std::vector<Color>& output,int block,int method,ParallelFor loop,void* context,UpscaleWorkspace* workspace){
 if(source.size()!=W*H||block<1||method<0||method>2){output.clear();return;}
 if(block==1||method==0){output=source;return;}
 int nx=(W+block-1)/block,ny=(H+block-1)/block,taps=method==1?2:4;
 UpscaleWorkspace local;auto& scratch=workspace?*workspace:local;
 if(scratch.block!=block||scratch.method!=method||scratch.x.size()!=W||scratch.y.size()!=H){
  scratch.x.resize(W);scratch.y.resize(H);
  for(int x=0;x<W;++x)scratch.x[x]=axis((x+.5f)/block-.5f,nx,method);
  for(int y=0;y<H;++y)scratch.y[y]=axis((y+.5f)/block-.5f,ny,method);
  scratch.block=block;scratch.method=method;
 }
 auto& ax=scratch.x;auto& ay=scratch.y;auto& horizontal=scratch.horizontal;
 horizontal.resize(W*ny);output.resize(W*H);
 struct Work {const std::vector<Color>& source;std::vector<Color>& output;std::vector<Vec>& horizontal;const std::vector<Axis>& ax;const std::vector<Axis>& ay;int block,taps;};
 Work work{source,output,horizontal,ax,ay,block,taps};
 auto first=[](int row,void* pointer){auto& w=*static_cast<Work*>(pointer);for(int x=0;x<W;++x){Vec sum;for(int j=0;j<w.taps;++j){Color c=w.source[row*w.block*W+w.ax[x].index[j]*w.block];sum=sum+Vec{float(c.r),float(c.g),float(c.b)}*w.ax[x].weight[j];}w.horizontal[row*W+x]=sum;}};
 auto second=[](int y,void* pointer){auto& w=*static_cast<Work*>(pointer);for(int x=0;x<W;++x){Vec sum;for(int j=0;j<w.taps;++j)sum=sum+w.horizontal[w.ay[y].index[j]*W+x]*w.ay[y].weight[j];w.output[y*W+x]={uint8_t(clamp(sum.x,0,255)+.5f),uint8_t(clamp(sum.y,0,255)+.5f),uint8_t(clamp(sum.z,0,255)+.5f)};}};
 if(loop){loop(ny,first,&work,context);loop(H,second,&work,context);}else{for(int y=0;y<ny;++y)first(y,&work);for(int y=0;y<H;++y)second(y,&work);}
}
}
