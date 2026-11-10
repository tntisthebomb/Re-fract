#pragma once
// Persistent std::thread worker pool; stands in for the 3DS StereoWorker.
// Drives the CPU renderer's batch shader and parallel-for through BatchQueue / LoopQueue.
#include "batch_queue.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
namespace rf {
class WorkerPool {
 std::vector<std::thread> threads;std::mutex m;std::condition_variable wake,idle;
 std::function<void()> task;uint64_t generation=0;int pending=0;bool stopping=false;
 void loop(){
  uint64_t seen=0;std::unique_lock<std::mutex> lock(m);
  for(;;){wake.wait(lock,[&]{return stopping||generation!=seen;});if(stopping)return;
   seen=generation;auto job=task;lock.unlock();job();lock.lock();if(--pending==0)idle.notify_one();}
 }
 // The caller helps; returns once every worker has finished its consumer.
 void run(const std::function<void()>& fn){
  if(threads.empty()){fn();return;}
  {std::lock_guard<std::mutex> lock(m);task=fn;pending=int(threads.size());++generation;}
  wake.notify_all();fn();
  std::unique_lock<std::mutex> lock(m);idle.wait(lock,[&]{return pending==0;});
 }
 public:
 void start(int total){for(int i=1;i<total;++i)threads.emplace_back([this]{loop();});}
 ~WorkerPool(){shutdown();}
 void shutdown(){{std::lock_guard<std::mutex> lock(m);stopping=true;}wake.notify_all();for(auto& t:threads)t.join();threads.clear();}
 int size()const{return int(threads.size())+1;}
 static void shade(const Scene& s,const Rays& r,const RenderJob* jobs,RenderResult* out,int n,void* self){
  auto& p=*static_cast<WorkerPool*>(self);BatchQueue q;q.reset(s,r,jobs,out,n);p.run([&]{q.consume();});
 }
 static void parallelFor(int n,LoopBody body,void* context,void* self){
  auto& p=*static_cast<WorkerPool*>(self);LoopQueue q;q.reset(n,body,context);p.run([&]{q.consume();});
 }
};
}
