#pragma once

#include <atomic>
#include <map>
#include <string>
#include <list>

#ifndef LMS_LEAKS_TRACING // 可能在外部构建命令中通过 -DLMS_LEAKS_TRACING=？指定，从而避免代码修改
#  define LMS_LEAKS_TRACING 0
#endif

namespace lms {

class Object {
protected:
  Object();
  virtual ~Object();

  // 声明为private，防止被直接调用
private:
  void ref();
  void unref(bool postphone);
    
private:
  std::atomic<int> refCount;
  
  // 赋予lms级别的几个资源管理方法以访问权限，以便调用者可以用下面几个更加便利的方法来进行引用计数管理
  template<class T> friend T retain(T);
  template<class T> friend T autoRelease(T);
  friend void release(Object*);
};

template<class T>
T retain(T object) {
  if (object == nullptr) {
    return nullptr;
  }
  
  object->ref();
  return object;
}

// 使用inline以避免链接过程提示duplicate symbol错误
inline void release(Object* object) {
  if (object == nullptr) {
    return;
  }
  
  object->unref(false);
  return object;
}

template<class T>
T autoRelease(T object) {
  if (object == nullptr) {
    return;
  }
  
  object->unref(true);
  return object;
}

void drainAutoReleasePool();


typedef struct {
} InitParams;

void init(InitParams params = {});
void unInit();

class DispatchQueue;

DispatchQueue *mainQueue();

typedef std::map<std::string, void*> Metadata;
typedef void Frame;

class FrameAcceptor : virtual public Object {
public:
  virtual void didReceiveFrame(Frame *frame) = 0;
};


class FrameSource : virtual public Object {
public:
  void addFrameAcceptor(FrameAcceptor *acceptor) {
    if (acceptor == nullptr) {
      return;
    }

    lms::retain(acceptor);
    acceptors.push_back(acceptor);
  }
  
  void removeFrameAcceptor(FrameAcceptor *acceptor) {
    acceptors.remove_if([acceptor] (FrameAcceptor *acc) {
      if (acc == acceptor) {
        lms::release(acc);
        return true;
      } else {
        return false;
      }
    });
  }
  
protected:
  void deliverFrame(Frame *frame) {
    for (auto acc : acceptors) {
      acc->didReceiveFrame(frame);
    };
  }
  
private:
  std::list<FrameAcceptor *> acceptors;
};


void dumpLeaks();

}