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
  void unref();
    
private:
  std::atomic<int> refCount;
  
  // 赋予lms级别的几个资源管理方法以访问权限，以便调用者可以用下面几个更加便利的方法来进行引用计数管理
  template<class T> friend T retain(T);
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
  
  object->unref();
  return object;
}

typedef std::map<std::string, void*> Metadata;
typedef void Frame;

struct Variant : virtual public Object {
  typedef enum {
    Bool          = 'b',
    Char          = 'c',
    Int           = 'i',
    UInt          = 'u',
    CString       = 's',
    OpaquePointer = '*',
    LMSObject     = 'o',
  } Type;
  
  Type type;

  union Value {
    bool          b;
    char          c;
    uint64_t      u;
    int64_t       i;
    const char   *cstr;
    void         *ptr;
    class Object *obj;
  } value;
  
  typedef void (*PFNReleaser)(Value& v);

  Variant(bool val) {
    construct(Bool);
    value.b = val;
  }
  
  Variant(char val) {
    construct(Char);
    value.c = val;
  }
  
  Variant(int64_t val) {
    construct(Int);
    value.i = val;
  }
  
  Variant(uint64_t val) {
    construct(UInt);
    value.u = val;
  }
  
  Variant(const char *val, bool copy = true) {
    if (copy) {
      construct(CString, releaseCString);
      value.cstr = strdup(val);
    } else {
      construct(CString);
      value.cstr = val;
    }
  }
  
  Variant(void *ptr, PFNReleaser releaser = nullptr) {
    construct(OpaquePointer, releaser);
    value.ptr = ptr;
  }
  
  Variant(Object *obj) {
    construct(LMSObject, releaseObject);
    value.obj = obj;
  }
  
  ~Variant() {
    if (releaser != nullptr) {
      releaser(value);
    }
  }
  
private:
  static void releaseCString(Value& v) {
    free((char *)v.cstr);
  }
  
  static void releaseObject(Value& v) {
    lms::release(v.obj);
  }
  
  void construct(Type type, PFNReleaser releaser = nullptr) {
    this->type     = type;
    this->releaser = releaser;
  }
  
  PFNReleaser releaser;
};

typedef std::map<std::string, Variant *> StreamMetaInfo;

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
