#include "engine.hpp"
#include <fstream>
#include <cstdio>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
namespace rf {
namespace {
using Bytes=std::vector<uint8_t>;
void encoded(void* context,void* bytes,int length){auto& out=*static_cast<Bytes*>(context);auto* begin=static_cast<uint8_t*>(bytes);out.insert(out.end(),begin,begin+length);}
bool jpeg(const std::vector<Color>& pixels,int width,int height,Bytes& out){return width>0&&height>0&&width<=4096&&height<=4096&&pixels.size()==size_t(width)*height&&stbi_write_jpg_to_func(encoded,&out,width,height,3,pixels.data(),95)!=0;}
void little(Bytes& out,uint32_t n,int count){for(int i=0;i<count;++i)out.push_back(uint8_t(n>>(i*8)));}
void entry(Bytes& out,uint16_t tag,uint16_t type,uint32_t count,uint32_t value){little(out,tag,2);little(out,type,2);little(out,count,4);little(out,value,4);}
Bytes mpf(bool primary,uint32_t firstSize,uint32_t secondSize){
 Bytes tiff{'I','I',42,0,8,0,0,0};
 if(primary){little(tiff,3,2);entry(tiff,0xB000,7,4,0x30303130);entry(tiff,0xB001,4,1,2);entry(tiff,0xB002,7,32,50);little(tiff,82,4);
  little(tiff,0x20020002,4);little(tiff,firstSize,4);little(tiff,0,4);little(tiff,0,4);
  little(tiff,0x00020002,4);little(tiff,secondSize,4);little(tiff,firstSize-10,4);little(tiff,0,4);
 }
 little(tiff,2,2);entry(tiff,0xB000,7,4,0x30303130);entry(tiff,0xB101,4,1,primary?1:2);little(tiff,0,4);
 Bytes segment{255,226};uint16_t length=uint16_t(tiff.size()+6);segment.push_back(length>>8);segment.push_back(length&255);segment.insert(segment.end(),{'M','P','F',0});segment.insert(segment.end(),tiff.begin(),tiff.end());return segment;
}
bool write(const Bytes& bytes,const std::string& path,std::string& error){std::ofstream out(path+".tmp",std::ios::binary);if(!out){error="Cannot write screenshot";return false;}out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();if(!out){std::remove((path+".tmp").c_str());error="Screenshot write failed";return false;}if(std::rename((path+".tmp").c_str(),path.c_str())!=0){std::remove((path+".tmp").c_str());error="Cannot finish screenshot";return false;}error="SCREENSHOT SAVED";return true;}
}
bool saveJPEG(const std::vector<Color>& pixels,int width,int height,const std::string& path,std::string& error){Bytes bytes;if(!jpeg(pixels,width,height,bytes)){error="JPEG encoding failed";return false;}return write(bytes,path,error);}
bool saveMPO(const std::vector<Color>& left,const std::vector<Color>& right,const std::string& path,std::string& error){
 Bytes a,b;if(!jpeg(left,W,H,a)||!jpeg(right,W,H,b)){error="Stereo JPEG encoding failed";return false;}
 auto first=mpf(true,0,0),second=mpf(false,0,0);first=mpf(true,uint32_t(a.size()+first.size()),uint32_t(b.size()+second.size()));
 Bytes out;out.reserve(a.size()+b.size()+first.size()+second.size());out.insert(out.end(),a.begin(),a.begin()+2);out.insert(out.end(),first.begin(),first.end());out.insert(out.end(),a.begin()+2,a.end());out.insert(out.end(),b.begin(),b.begin()+2);out.insert(out.end(),second.begin(),second.end());out.insert(out.end(),b.begin()+2,b.end());return write(out,path,error);
}
}
