#ifdef __3DS__
#include "gpu_surface.hpp"
#include "mesh_shbin.h"
#include <cstring>
namespace rf {
void GpuSurface::synchronize(){
 if(initialized)C3D_FrameSync();
}
bool GpuSurface::upload(const SurfaceMesh& mesh){
 constexpr size_t maxVertices=(W/4)*(H/4),maxIndices=(W/4-1)*(H/4-1)*6;
 if(mesh.vertices.empty()||mesh.vertices.size()>maxVertices||mesh.indices.empty()||mesh.indices.size()>maxIndices||mesh.indices.size()%3)return false;
 for(auto index:mesh.indices)if(index>=mesh.vertices.size())return false;
 if(!initialized){
  if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))return false;
  initialized=true;
  shader=DVLB_ParseFile((u32*)mesh_shbin,mesh_shbin_size);if(!shader){shutdown();return false;}
  shaderProgramInit(&program);shaderProgramSetVsh(&program,&shader->DVLE[0]);
  if(!program.vertexShader){shutdown();return false;}
  projectionLocation=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
  vertices=static_cast<SurfaceVertex*>(linearAlloc(maxVertices*sizeof(SurfaceVertex)));
  indices=static_cast<uint16_t*>(linearAlloc(maxIndices*sizeof(uint16_t)));
  if(!vertices||!indices||projectionLocation<0){shutdown();return false;}
  constexpr u32 transfer=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
  for(int eye=0;eye<2;++eye){targets[eye]=C3D_RenderTargetCreate(H,W,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);
   if(!targets[eye]){shutdown();return false;}C3D_RenderTargetSetOutput(targets[eye],GFX_TOP,eye?GFX_RIGHT:GFX_LEFT,transfer);
  }
 }else synchronize();
 // Reuse shaders, targets and fixed-capacity buffers; wait before overwriting GPU inputs.
 ready=false;
 std::memcpy(vertices,mesh.vertices.data(),mesh.vertices.size()*sizeof(SurfaceVertex));
 std::memcpy(indices,mesh.indices.data(),mesh.indices.size()*sizeof(uint16_t));
 if(R_FAILED(GSPGPU_FlushDataCache(vertices,mesh.vertices.size()*sizeof(SurfaceVertex)))||R_FAILED(GSPGPU_FlushDataCache(indices,mesh.indices.size()*sizeof(uint16_t))))return false;
 origin=mesh.origin;count=int(mesh.indices.size());ready=true;return true;
}
bool GpuSurface::draw(const Scene& s,float slider){
 if(!available()||!C3D_FrameBegin(0))return false;
 C3D_BindProgram(&program);
 auto* attrs=C3D_GetAttrInfo();AttrInfo_Init(attrs);AttrInfo_AddLoader(attrs,0,GPU_FLOAT,3);AttrInfo_AddLoader(attrs,1,GPU_FLOAT,3);
 auto* buffers=C3D_GetBufInfo();BufInfo_Init(buffers);BufInfo_Add(buffers,vertices,sizeof(SurfaceVertex),2,0x10);
 for(int i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
 auto* env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
 C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
 int eyes=s.settings.stereo&&slider>0&&s.settings.eyeSeparation>0?2:1;
 for(int eye=0;eye<eyes;++eye){float offset=eyes==2?(eye?1.f:-1.f)*s.settings.eyeSeparation*slider*.5f:0;
  auto matrix=meshProjection(s,offset,origin);C3D_Mtx projection;
  for(int row=0;row<4;++row){projection.r[row].x=matrix[row*4];projection.r[row].y=matrix[row*4+1];projection.r[row].z=matrix[row*4+2];projection.r[row].w=matrix[row*4+3];}
  uint32_t sky=(uint32_t(clamp(s.settings.skyColor.x*s.settings.exposure,0,1)*255)<<24)|(uint32_t(clamp(s.settings.skyColor.y*s.settings.exposure,0,1)*255)<<16)|(uint32_t(clamp(s.settings.skyColor.z*s.settings.exposure,0,1)*255)<<8)|255;
  C3D_RenderTargetClear(targets[eye],C3D_CLEAR_ALL,sky,0);C3D_FrameDrawOn(targets[eye]);
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,projectionLocation,&projection);C3D_DrawElements(GPU_TRIANGLES,count,C3D_UNSIGNED_SHORT,indices);
 }
 // VBO/IBO were flushed at upload; only flush newly emitted command lists.
 C3D_FrameEnd(GX_CMDLIST_FLUSH);return true;
}
bool GpuSurface::screenshot(int eye,std::vector<Color>& pixels){
 if(!available()||eye<0||eye>1||!targets[eye])return false;
 C3D_FrameSync();
 u8* output=static_cast<u8*>(linearAlloc(W*H*3));if(!output)return false;
 constexpr u32 flags=GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
 C3D_SyncDisplayTransfer(static_cast<u32*>(targets[eye]->frameBuf.colorBuf),GX_BUFFER_DIM(H,W),reinterpret_cast<u32*>(output),GX_BUFFER_DIM(H,W),flags);
 if(R_FAILED(GSPGPU_InvalidateDataCache(output,W*H*3))){linearFree(output);return false;}
 pixels.resize(W*H);for(int x=0;x<W;++x)for(int y=0;y<H;++y){size_t k=(x*H+H-1-y)*3;pixels[y*W+x]={output[k+2],output[k+1],output[k]};}
 linearFree(output);return true;
}
void GpuSurface::shutdown(){
 if(initialized){C3D_FrameSync();for(auto& target:targets)if(target){C3D_RenderTargetDelete(target);target=nullptr;}C3D_Fini();initialized=false;}
 if(vertices){linearFree(vertices);vertices=nullptr;}if(indices){linearFree(indices);indices=nullptr;}
 if(shader){shaderProgramFree(&program);DVLB_Free(shader);shader=nullptr;}
 targets[0]=targets[1]=nullptr;ready=false;count=0;
}
}
#endif
