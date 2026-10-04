#pragma once
#include "engine.hpp"
#include <atomic>
namespace rf {
// Initialize before publishing the request; join every consumer before reuse.
// Each result slot has exactly one writer, regardless of relative core speed.
class BatchQueue {
 const Scene* scene=nullptr;const Rays* rays=nullptr;
 const RenderJob* jobs=nullptr;RenderResult* results=nullptr;
 int count=0;std::atomic<int> next{0};
 public:
 void reset(const Scene& s,const Rays& r,const RenderJob* j,RenderResult* out,int n){scene=&s;rays=&r;jobs=j;results=out;count=n;next.store(0,std::memory_order_relaxed);}
 void consume(){for(;;){int i=next.fetch_add(1,std::memory_order_relaxed);if(i>=count)return;results[i]=renderJob(*scene,*rays,jobs[i]);}}
};
}
