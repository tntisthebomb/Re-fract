// Regression test: progressive lighting / interlaced passes must not form row, column or diagonal line patterns.
// Build (from windows/):  g++ -std=c++17 -O2 -Iengine tests/lighting_stripes.cpp engine/expression.cpp engine/formula.cpp \
//    engine/renderer.cpp engine/storage.cpp engine/surface.cpp engine/upscale.cpp engine/screenshot.cpp -o /tmp/lighting_stripes
// Checks: (1) scatterCell() is a bijection and the first interlace lane covers every column and row of the cell grid
// (cell = lane + index*lanes alone is column-aligned when the grid width is a multiple of the lane count);
// (2) mid-pass of a progressive-lighting render, the per-column error against the finished image has no period-`lanes` pattern.
#include "engine.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace rf;
static int failures=0;
static void check(bool ok,const char* what){if(!ok){std::printf("FAIL: %s\n",what);++failures;}else std::printf("ok:   %s\n",what);}
// Peak-to-peak of the column means of `v` folded at `period`, relative to the overall mean.
static double phaseContrast(const std::vector<double>& columnMeans,int period){
 double total=0;for(double c:columnMeans)total+=c;double mean=total/columnMeans.size();
 double lo=1e30,hi=-1e30;for(int k=0;k<period;++k){double s=0;int n=0;for(size_t i=k;i<columnMeans.size();i+=period){s+=columnMeans[i];++n;}s/=n;lo=std::fmin(lo,s);hi=std::fmax(hi,s);}
 return mean>0?(hi-lo)/mean:0;
}
int main(){
 for(uint32_t n:{3u,5u,1000u,6000u,24000u,43200u}){std::vector<char> seen(n,0);bool ok=true;for(uint32_t i=0;i<n&&ok;++i){uint32_t c=scatterCell(i,n);ok=c<n&&!seen[c];if(ok)seen[c]=1;}check(ok,"scatterCell is a bijection");}
 for(int lanes:{2,3,4,8})for(int nx:{100,64,97}){
  const int ny=60,total=nx*ny;std::vector<int> col(nx,0),row(ny,0);
  for(int index=0;index*lanes<total;++index){uint32_t cell=scatterCell(uint32_t(index*lanes),uint32_t(total));++col[cell%nx];++row[cell/nx];}
  bool covered=true;for(int c:col)covered&=c>0;for(int r:row)covered&=r>0;check(covered,"first interlace lane touches every column and row");
 }
 // (2) mid-pass image statistics at 200x120 (width divisible by the 4 lanes).
 const int w=200,h=120,lanes=4;
 Scene s;s.formula=preset(0);s.camera=presetCamera(0);s.settings.stereo=false;
 s.settings.progressiveLighting=true;s.settings.giSamples=2;s.settings.lightingPasses=2;s.settings.lightingBlock=1;s.settings.interlace=lanes;s.settings.refineSamples=1;
 for(int mode=0;mode<2;++mode){
  scatterPasses=mode==1;
  W=w;H=h;Renderer finish,mid;finish.resize(w,h);mid.resize(w,h);
  finish.invalidate(s,false);Rays rays(s);finish.beginFrame(s,false,true,0);int guard=0;while(!finish.complete()&&guard++<2000000)finish.step(s,rays,0);
  mid.invalidate(s,false);mid.beginFrame(s,false,true,0);guard=0;
  while(!mid.complete()&&guard++<2000000){mid.step(s,rays,0);if(mid.currentBlock()==1&&mid.progress(s)>0.2f)break;}
  const auto& a=finish.image(0);const auto& b=mid.image(0);std::vector<double> colErr(w,0);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){const Color p=a[y*w+x],q=b[y*w+x];colErr[x]+=(std::abs(p.r-q.r)+std::abs(p.g-q.g)+std::abs(p.b-q.b))/double(h);}
  double contrast=phaseContrast(colErr,lanes);
  std::printf("%s order: period-%d column error contrast = %.3f\n",mode?"scatter":"raster",lanes,contrast);
  if(mode==1)check(contrast<0.4,"scattered pass order leaves no column pattern mid-pass");
 }
 std::printf("%s\n",failures?"FAILED":"PASSED");return failures?1:0;
}
