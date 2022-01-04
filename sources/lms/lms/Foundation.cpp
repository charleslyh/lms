#include "Foundation.h"
#include "Logger.h"
#include "Runtime.h"
#include "Stacktrace.h"
#include <cstdio>
#include <list>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <unordered_map>

extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

#if (LMS_LEAKS_TRACING)
class LeaksTracer {
  std::unordered_map<Object *, std::list<std::string>> traces;
  SDL_mutex *mtx;
  
public:
  LeaksTracer() {
    mtx = SDL_CreateMutex();
  }
  
  ~LeaksTracer() {
    SDL_DestroyMutex(mtx);
  }
  
  void add(Object *obj) {
    SDL_LockMutex(mtx);
    traces[obj] = std::list<std::string>();
    SDL_UnlockMutex(mtx);
  }
  
  void remove(Object *obj) {
    SDL_LockMutex(mtx);
    traces.erase(obj);
    SDL_UnlockMutex(mtx);
  }
  
  void mark(Object *obj, const char *type, int offset) {
    SDL_LockMutex(mtx);
    {
      bool objExist = (traces.find(obj) != traces.end());
      if (objExist) {
        std::list<std::string>& itsTraces = traces[obj];
        const char *trace = stacktrace_caller_frame_desc(type, offset + 1);
        itsTraces.push_back(trace);
        free((void *)trace);
      }
    }
    SDL_UnlockMutex(mtx);
  }

  void dumpLeaks() {
    LMSLogVerbose("Leaks: %d", (int)traces.size());
    
    if (!traces.empty()) {
      LMSLogVerbose("--- Start ---");
      for (auto item : traces) {
        LMSLogVerbose("obj: %p", item.first);
        for (auto& trace : item.second) {
          LMSLogVerbose("  %s", trace.c_str());
        }
      }
      LMSLogVerbose("--- End ---");
    }
  }
};

static LeaksTracer __leaksTracer;
#  define MarkObject(obj, type, offset) __leaksTracer.mark(obj, type, offset)
#  define TraceObject(obj)              __leaksTracer.add(obj)
#  define UntraceObject(obj)            __leaksTracer.remove(obj)
#  define DumpLeaks()                   __leaksTracer.dumpLeaks()
#else
#  define TraceObject(obj)
#  define UntraceObject(obj)
#  define MarkObject(obj, type, offset)
#  define DumpLeaks()
#endif // LMS_LEAKS_TRACING

void dumpLeaks() {
  DumpLeaks();
}

Object::Object() :refCount(1) {
  TraceObject(this);
  
  // 这里只能最终到下一级子类名，如果有多级继承，则无法准确还原，因此应尽可能减少继承层级
  // 0: 当前Object的构造函数
  // 1: 子类构造函数
  MarkObject(this, "N", 1);
}

Object::~Object() {
  UntraceObject(this);
}

void Object::ref() {
  refCount += 1;
  
  // 仅当ref方法全局只有 lms::retain 一个调用者，则能保证栈跟踪的正确性
  // 0: 当前ref函数
  // 1: lms::retain
  // 2: lms::retain的调用者
  MarkObject(this, "R", 2);
}

void Object::unref() {
  int newCount = (refCount -= 1);
  assert(newCount >= 0); // < 0 意味着ref、unref不匹配：unref过多

  if (newCount <= 0) {
    delete this;
  } else {
    // 仅当还无法真正delete时，才有必要进行跟踪
    // 0: 当前unref(false)函数
    // 1: lms::release函数
    // 2: lms::release的调用者
    MarkObject(this, "U", 2);
  }
}

#define IMPLEMENT_VARIANTS_GETTER(RTYPE, VTYPE, ValueField)\
RTYPE variantsGet##VTYPE(const Variants& variants, const std::string& key, RTYPE defaultValue) {\
  auto it = variants.find(key);\
  if (it == variants.end()) {\
    return defaultValue;\
  }\
\
  if (it->second.type != Variant::VTYPE) {\
    return defaultValue;\
  }\
\
  return it->second.value.ValueField;\
}

IMPLEMENT_VARIANTS_GETTER(bool        ,Bool      ,b);
IMPLEMENT_VARIANTS_GETTER(int64_t     ,Int       ,i);
IMPLEMENT_VARIANTS_GETTER(uint64_t    ,UInt      ,u);
IMPLEMENT_VARIANTS_GETTER(char        ,Char      ,c);
IMPLEMENT_VARIANTS_GETTER(const char* ,CString   ,cstr);
IMPLEMENT_VARIANTS_GETTER(void*       ,Pointer   ,ptr);
IMPLEMENT_VARIANTS_GETTER(Object*     ,LMSObject ,obj);

}
