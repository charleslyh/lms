//
//  Runtime.h
//  lms
//
//  Created by yuhuachli on 2021/11/29.
//

#pragma once

#include <lms/Foundation.h>
#include <functional>

namespace lms {

/*
 @class Runnable
 希望被DispatchQueue执行的任务接口。通过继承实现该接口，可以向DispatchQueue中插入任意
 待执行的任务。
 */
class Runnable : virtual public Object {
public:
  /*
   @function run
   任务的执行接口
   */
  virtual void run() = 0;
  
  uint32_t enqueueTS;
};

class LambdaRunnable : public Runnable {
public:
  LambdaRunnable(std::function<void()> a) : act(a) { }
  
  void run() override {
    act();
  }
  
private:
  std::function<void()> act;
};

class DispatchQueue : virtual public Object {
public:
  /*
   @function dispatch
   向DispatchQueue中添加一个待执行的任务
   
   @param runnable 待执行的任务实例
   */
  virtual void enqueue(void *sender, Runnable *runnable) = 0;
  
  /*
   @function cancel
   取消队列中的待执行任务
   
   @param sender 任务发起者的标识指针，由DispatchQueue::async的sender参数传入。
                 如果sender为nullptr，则会取消该DispatchQueue中所有待执行的任务。否则，
                 只会取消对应sender发起的任务。
   */
  virtual void cancel(void *sender) = 0;
  
  virtual void scheduleOnce() = 0;
  
  virtual bool isMainThread() = 0;
};

DispatchQueue *mainQueue();

bool isMainThread();

// TODO: 既然业务能拿到DispatchQueue实例，为什么还需要下面两个方法？swift中的API是怎样的？
void async(DispatchQueue *queue, void *sender, Runnable *runnable);
void async(DispatchQueue *queue, void *sender, std::function<void()> action);

class Timer : virtual public Object {};

// Creates a timer and schedules it on a new thread.
Timer *scheduleTimer(const char *name, double interval, std::function<void()> action);

// Invalidates the timer scheduled by scheduleTimer()
void invalidateTimer(Timer *timer);

}
