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

struct VariantRef {
  std::atomic<int> count;
  
  VariantRef() {
    count = 1;
  }
};

struct Variant : virtual public Object {
  typedef enum {
    None          = 0,
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
    Object       *obj;
  } value;
  
  typedef Value (*PFNRetain)(const Value& from);
  typedef void (*PFNRelease)(Value& v);
  
  Variant() {
    construct(None);
    value = {0};
  }

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
      construct(CString, copyCString, releaseCString);
      value.cstr = strdup(val);
    } else {
      construct(CString);
      value.cstr = val;
    }
  }
  
  Variant(void *ptr, PFNRetain retainer = nullptr, PFNRelease releaser = nullptr) {
    construct(OpaquePointer, retainer, releaser);
    
    if (retainer) {
      Value tmp;
      tmp.ptr = ptr;
      value = retainer(tmp);
    } else {
      value.ptr = ptr;
    }
  }
  
  Variant(Object *obj) {
    construct(LMSObject, retainObject, releaseObject);
    
    retain(obj);
    value.obj = obj;
  }
  
  Variant(const Variant& origin) {
    construct(origin.type, origin.retainer, origin.releaser, origin.ref);
    
    if (origin.retainer) {
      value = origin.retainer(origin.value);
    } else {
      this->value = origin.value;
    }
  }
  
  Variant& operator=(const Variant& rhs) {
    // TODO: assert(rhs.type == this->type);
    if (type != None && type != rhs.type) {
      return *this;
    }
    
    // 先释放自身持有的资源
    int newCount =  ref->count - 1;
    if (newCount <= 0 && releaser) {
      releaser(value);
      delete ref;
    }

    construct(type, rhs.retainer, rhs.releaser, rhs.ref);

    if (rhs.retainer) {
      value = rhs.retainer(rhs.value);
    } else {
      value = rhs.value;
    }
    
    return *this;
  }
  
  ~Variant() {
    int newCount = ref->count - 1;
    if (newCount == 0 && releaser != nullptr) {
      releaser(value);
      delete ref;
    }
  }
  
private:
  static Value copyCString(const Value& from) {
    Value v;
    v.cstr = strdup(from.cstr);
    return v;
  }
  
  static void releaseCString(Value& v) {
    free((char *)v.cstr);
  }
  
  static Value retainObject(const Value& from) {
    Value v;
    v.obj = lms::retain(from.obj);
    return v;
  }
  
  static void releaseObject(Value& v) {
    lms::release(v.obj);
  }
  
  void construct(Type type, PFNRetain retainer = nullptr, PFNRelease releaser = nullptr, VariantRef *ref = nullptr) {
    this->type     = type;
    this->retainer = retainer;
    this->releaser = releaser;
    
    if (!ref) {
      this->ref = new VariantRef;
    } else {
      this->ref = ref;
      this->ref->count += 1;
    }
  }
  
  PFNRetain  retainer;
  PFNRelease releaser;
  VariantRef *ref;
};

typedef std::map<std::string, Variant> StreamMetaInfo;

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
