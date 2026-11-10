#pragma once
// Minimal OpenGL 3.3 core loader (functions fetched via SDL_GL_GetProcAddress).
// Call rf::loadGL() once after creating a GL 3.3+ core context. Both the GPU
// renderer and the window/display code use these entry points.
#include <SDL.h>
#include <SDL_opengl.h>
#ifndef GL_COMPUTE_SHADER
#endif
namespace rf {
#define RF_GL_FUNCS(X) \
 X(PFNGLCREATESHADERPROC,glCreateShader) X(PFNGLSHADERSOURCEPROC,glShaderSource) X(PFNGLCOMPILESHADERPROC,glCompileShader) \
 X(PFNGLGETSHADERIVPROC,glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC,glGetShaderInfoLog) X(PFNGLDELETESHADERPROC,glDeleteShader) \
 X(PFNGLCREATEPROGRAMPROC,glCreateProgram) X(PFNGLATTACHSHADERPROC,glAttachShader) X(PFNGLLINKPROGRAMPROC,glLinkProgram) \
 X(PFNGLGETPROGRAMIVPROC,glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC,glGetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC,glDeleteProgram) \
 X(PFNGLUSEPROGRAMPROC,glUseProgram) X(PFNGLGETUNIFORMLOCATIONPROC,glGetUniformLocation) \
 X(PFNGLUNIFORM1IPROC,glUniform1i) X(PFNGLUNIFORM1FPROC,glUniform1f) X(PFNGLUNIFORM2FPROC,glUniform2f) X(PFNGLUNIFORM3FPROC,glUniform3f) \
 X(PFNGLUNIFORM4FPROC,glUniform4f) X(PFNGLUNIFORM1FVPROC,glUniform1fv) X(PFNGLUNIFORM4FVPROC,glUniform4fv) X(PFNGLUNIFORM3FVPROC,glUniform3fv) \
 X(PFNGLUNIFORMMATRIX3FVPROC,glUniformMatrix3fv) \
 X(PFNGLGENVERTEXARRAYSPROC,glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC,glBindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC,glDeleteVertexArrays) \
 X(PFNGLGENFRAMEBUFFERSPROC,glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC,glBindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC,glFramebufferTexture2D) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,glCheckFramebufferStatus) X(PFNGLDELETEFRAMEBUFFERSPROC,glDeleteFramebuffers) \
 X(PFNGLACTIVETEXTUREPROC,glActiveTexture)
#define X(type,name) extern type name;
RF_GL_FUNCS(X)
#undef X
bool loadGL(); // false if any entry point is missing
}
