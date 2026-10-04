#include "engine.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
using namespace rf;
int main(int argc,char** argv){
 std::ofstream image;if(argc>1)image.open(argv[1],std::ios::binary);
 std::cout<<"preset,ms,rays,checksum\n";
 for(int n:{0,6,10,16}){Scene s;s.formula=preset(n);s.camera=presetCamera(n);s.settings.farClip=40;s.settings.convergence=-s.camera.position.z;Rays rays(s);
  uint64_t checksum=0;auto start=std::chrono::steady_clock::now();
  for(int y=0;y<240;y+=8)for(int x=0;x<400;x+=8){Color c=trace(s,rays,x+4,y+4,0);checksum=checksum*131+c.r*65536+c.g*256+c.b;if(image){char b[]={char(c.r),char(c.g),char(c.b)};image.write(b,3);}}
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();std::cout<<n<<','<<ms<<",1500,"<<checksum<<'\n';
 }
}
