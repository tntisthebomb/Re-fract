#pragma once
#include <3ds.h>
#include <atomic>
#include "engine.hpp"

namespace rf {
// Only the right-eye color is touched by the worker. Scene/basis live until join
// at the end of each cell; framebuffer writes remain exclusively on the main thread.
class StereoWorker {
 Thread thread=nullptr;LightEvent request,finished;
 std::atomic<bool> stop{false},pending{false},ready{false};
 const Scene* scene=nullptr;const Rays* rays=nullptr;
 int x=0,y=0,block=1,count=1;float eye=0;Color result;
 static void run(void* arg){auto& w=*static_cast<StereoWorker*>(arg);
  for(;;){LightEvent_Wait(&w.request);if(w.stop.load(std::memory_order_acquire))break;
   if(!w.pending.exchange(false,std::memory_order_acquire))continue;
   w.result=shadeCell(*w.scene,*w.rays,w.x,w.y,w.block,w.eye,w.count);
   w.ready.store(true,std::memory_order_release);LightEvent_Signal(&w.finished);
  }
 }
 public:
 explicit StereoWorker(bool enabled){
  LightEvent_Init(&request,RESET_ONESHOT);LightEvent_Init(&finished,RESET_ONESHOT);
  if(enabled)thread=threadCreate(run,this,24*1024,0x30,2,false);
 }
 bool available()const{return thread!=nullptr;}
 void shutdown(){if(thread){stop.store(true,std::memory_order_release);LightEvent_Signal(&request);threadJoin(thread,U64_MAX);threadFree(thread);thread=nullptr;}}
 ~StereoWorker(){shutdown();}
 static void shade(const Scene& s,const Rays& r,int x,int y,int block,float eye,int count,Color& left,Color& right,void* arg){
  auto& w=*static_cast<StereoWorker*>(arg);
  w.scene=&s;w.rays=&r;w.x=x;w.y=y;w.block=block;w.eye=eye;w.count=count;
  w.ready.store(false,std::memory_order_relaxed);w.pending.store(true,std::memory_order_release);LightEvent_Signal(&w.request);
  left=shadeCell(s,r,x,y,block,-eye,count);
  LightEvent_Wait(&w.finished);
  if(w.ready.load(std::memory_order_acquire))right=w.result;
 }
};
}
