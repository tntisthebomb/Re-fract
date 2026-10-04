#pragma once
#include "engine.hpp"
#include <algorithm>
namespace rf {
// Landscape RGB -> rotated column-major BGR.
inline void convertFramebuffer(uint8_t* frame,const Color* pixels,int width){
 for(int x=0;x<width;++x)for(int y=0;y<H;++y){const Color& c=pixels[y*width+x];size_t k=(x*H+H-1-y)*3;frame[k]=c.b;frame[k+1]=c.g;frame[k+2]=c.r;}
}
}
