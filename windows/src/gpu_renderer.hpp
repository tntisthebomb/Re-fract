#pragma once
// GPU (OpenGL 3.3 core) ray-marcher. Generates a GLSL fragment shader from the
// Formula (stage structure + expression text baked in, numeric parameters passed
// as uniforms) and mirrors the CPU sampleRay() in windows/engine/renderer.cpp:
// ray steps, relaxation off, epsilon/fog/AO/shadow/specular/palette/exposure,
// quality mode (steps*2 capped 512, eps*.5, exact bulb path, 1/2/4 subsamples),
// repeat boundary stepping, terminal shapes, Julia, expressions with dual-number
// derivatives, invalid => magenta (180,20,90).
#include "engine.hpp"
#include <string>
namespace rf {
class GpuRenderer {
 struct Impl; Impl* impl=nullptr;
 public:
 GpuRenderer(); ~GpuRenderer();
 GpuRenderer(const GpuRenderer&)=delete; GpuRenderer& operator=(const GpuRenderer&)=delete;
 // True if this GPU path reproduces every enabled feature of the scene (stages, lighting, DOF, GI, upscale...).
 // If false, `reason` names the first unsupported feature and the app should render that scene on the CPU backend.
 static bool supports(const Scene& scene,std::string& reason);
 // Requires a current GL 3.3 core context and rf::loadGL() already succeeded.
 bool init(std::string& error);
 void shutdown();
 // Allocates RGBA8 render targets (one per eye) of this size. Safe to call repeatedly.
 bool resize(int width,int height);
 // Starts a new image. eyeOffset==0 => mono (1 eye); else 2 eyes at -eyeOffset (left) / +eyeOffset (right)
 // (same convention as CPU RenderJob.offset). Recompiles the shader only if formula structure changed;
 // on compile failure returns false and fills error (previous shader stays usable).
 bool begin(const Scene& scene,float eyeOffset,std::string& error);
 // Renders the visible region [0,renderW)x[0,renderH) of the target (renderW/H <= resize() size; lets the
 // app render at reduced scale while moving). Draws in horizontal strips, stopping after budgetMs
 // (adaptive strip height to avoid Windows TDR). Returns true when the whole image is complete.
 // Call repeatedly each frame until true; calling begin() restarts.
 bool render(const Scene& scene,int renderW,int renderH,float budgetMs);
 int eyes() const; // 1 or 2 for the image started by begin()
 unsigned texture(int eye) const; // GL_TEXTURE_2D, RGBA8, LINEAR-filterable, origin bottom-left (GL convention)
 // Reads back the eye image as top-down RGB (for PNG/PPM export). Size is the full resize() size region [0,w)x[0,h) last rendered.
 bool readback(int eye,std::vector<Color>& out,int& width,int& height);
 const char* rendererName() const; // GL_RENDERER string
 // Progressive refinement: passes published to the display texture so far (min over eyes) / passes the image will have.
 // The image is complete and upright after every published pass; render() returns true when passesDone()==passesTotal().
 int passesDone() const;
 int passesTotal() const;
};
}
