#include "gl_loader.hpp"
namespace rf {
#define X(type,name) type name=nullptr;
RF_GL_FUNCS(X)
#undef X
bool loadGL(){
 bool ok=true;
#define X(type,name) name=reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));ok=ok&&name;
 RF_GL_FUNCS(X)
#undef X
 return ok;
}
}
