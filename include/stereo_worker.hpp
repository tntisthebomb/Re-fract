#pragma once
#include <3ds.h>
#include <atomic>
#include "batch_queue.hpp"
namespace rf {
// Both CPUs claim independent jobs; only the main thread writes image buffers.
class StereoWorker {
 Thread thread=nullptr;LightEvent request,finished;
 std::atomic<bool> stop{false},pending{false},ready{false};
 BatchQueue queue;
 static void run(void* arg){auto& w=*static_cast<StereoWorker*>(arg);
  for(;;){LightEvent_Wait(&w.request);if(w.stop.load(std::memory_order_acquire))break;
   if(!w.pending.exchange(false,std::memory_order_acquire))continue;
   w.queue.consume();
   w.ready.store(true,std::memory_order_release);LightEvent_Signal(&w.finished);
  }
 }
 public:
 explicit StereoWorker(bool enabled){LightEvent_Init(&request,RESET_ONESHOT);LightEvent_Init(&finished,RESET_ONESHOT);if(enabled)thread=threadCreate(run,this,24*1024,0x30,2,false);}
 bool available()const{return thread!=nullptr;}
 void shutdown(){if(thread){stop.store(true,std::memory_order_release);LightEvent_Signal(&request);threadJoin(thread,U64_MAX);threadFree(thread);thread=nullptr;}}
 ~StereoWorker(){shutdown();}
 static void shade(const Scene& s,const Rays& r,const RenderJob* jobs,RenderResult* results,int count,void* arg){
  auto& w=*static_cast<StereoWorker*>(arg);w.queue.reset(s,r,jobs,results,count);
  w.ready.store(false,std::memory_order_relaxed);w.pending.store(true,std::memory_order_release);LightEvent_Signal(&w.request);
  w.queue.consume();LightEvent_Wait(&w.finished);
  // Acquire publishes all results written by the worker before returning.
  while(!w.ready.load(std::memory_order_acquire)){}
 }
};
}
