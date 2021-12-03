#pragma once

#include <lms/Foundation.h>

namespace lms {

// 特定数据加载模块(如avformat)会使用特定的数据结构来作为数据容器
// 这个对于Packet来说应该是透明的。但是Packet又需要知道如何释放它，所以需要一个Holder来
// 帮助进行真实容器的生命周期管理
class ResourceHolder : virtual public Object {
public:
  virtual void* retain(void *object) = 0;
  virtual void release(void *object) = 0;
};

class Packet : virtual public Object {
public:
  int     streamIndex;
  uint8_t *data;
  int     size;
  int64_t pts;

public:
  Packet(void *internalPacket, ResourceHolder *holder);
  ~Packet();
  
private:
  ResourceHolder *holder;
  void *internalPacket;
};

}
