//
//  Events.cpp
//  lms
//
//  Created by yuhuachli on 2022/1/4.
//

#include "Events.h"
#include "Module.h"
#include <list>
extern "C" {
#include <SDL2/SDL.h>
}

namespace lms {

class EventCenter;
class DispatchQueue;

static EventCenter   *_eventCenter;

struct EventObserver {
  const char   *name;
  void         *sender;
  EventHandler *handler;

  EventObserver(const char *name, void *sender, EventHandler *handler) {
    this->name    = strdup(name);
    this->sender  = sender;
    this->handler = lms::retain(handler);
  }
  
  ~EventObserver() {
    lms::release(handler);
    free((void *)name);
  }
};

class EventCenter {
public:
  void addObserver(EventObserver *o) {
    assert(hostQueue());
    observers.push_back(o);
  }
  
  void removeObserver(EventObserver *o) {
    observers.remove(o);
  }
  
  void dispatchEvent(const char *name, void *sender, const EventParams& params) {
    std::string nm = name;
    lms::async(hostQueue(), [this, nm, sender, params] () {
      fire(nm.c_str(), sender, params);
    });
  }

  std::list<EventObserver *> observers;

private:
  void fire(const char *name, void *sender, const EventParams& params) {
    for (auto o : observers) {
      if (strcmp(name, o->name) == 0 && (o->sender == nullptr || o->sender == sender)) {
        o->handler->handleEvent(name, sender, params);
      }
    }
  }
};

class LambdaEventHandler : public EventHandler {
public:
  LambdaEventHandler(std::function<void(const char *, void *, const EventParams&)> block) {
    this->block = block;
  }
  
  void handleEvent(const char *name, void *sender, const EventParams& params) {
    block(name, sender, params);
  }
  
  std::function<void(const char *, void *, const EventParams&)> block;
};

class CallbackEventHandler : public EventHandler {
public:
  CallbackEventHandler(void *context, EventCallback callback) {
    this->context  = context;
    this->callback = callback;
  }
  
  void handleEvent(const char *name, void *sender, const EventParams& params) {
    callback(context, name, sender, params);
  }

  void *context;
  EventCallback callback;
};

void* addEventObserver(const char *name, void *sender, EventHandler *handler) {
  EventObserver *o = new EventObserver(name, sender, handler);
  _eventCenter->addObserver(o);
  return o;
}

void* addEventObserver(const char *name, void *sender, std::function<void(const char *, void *, const EventParams&)> block) {
  EventHandler *handler = new LambdaEventHandler(block);
  void *obs = addEventObserver(name, sender, handler);
  lms::release(handler);

  return obs;
}

void* addEventObserver(const char *name, void *sender, void *context, EventCallback evtCallback) {
  EventHandler *handler = new CallbackEventHandler(context, evtCallback);
  void *obs = addEventObserver(name, sender, handler);
  lms::release(handler);

  return obs;
}

void removeEventObserver(void *observer) {
  EventObserver *o = (EventObserver *)observer;
  _eventCenter->removeObserver(o);
  delete o;
}

void fireEvent(const char *name, void *sender, const EventParams& params) {
  _eventCenter->dispatchEvent(name, sender, params);
}

// ---

static void setupEventCenter() {
  _eventCenter = new EventCenter;
}

static void teardownEventCenter() {
  assert(_eventCenter->observers.empty());
  assert(isHostThread());
    
  delete _eventCenter;
  _eventCenter = nullptr;
}

Module moduleEventCenter = {
  .name     = "EventCenter",
  .setup    = setupEventCenter,
  .teardown = teardownEventCenter
};

}
