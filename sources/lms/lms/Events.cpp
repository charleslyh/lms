//
//  Events.cpp
//  lms
//
//  Created by yuhuachli on 2022/1/4.
//

#include "Events.h"
#include "Module.h"
#include <list>

namespace lms {

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
  std::list<EventObserver *> observers;
  
  void addObserver(EventObserver *o) {
    observers.push_back(o);
  }
  
  void removeObserver(EventObserver *o) {
    observers.remove(o);
  }
  
  void fire(const char *name, void *sender, const EventParams& params) {
    for (auto o : observers) {
      if (strcmp(name, o->name) == 0 && (o->sender == nullptr || o->sender == sender)) {
        o->handler->handleEvent(name, sender, params);
      }
    }
  }
};

static EventCenter *_eventCenter;

void* addEventObserver(const char *name, void *sender, EventHandler *handler) {
  EventObserver *o = new EventObserver(name, sender, handler);
  _eventCenter->addObserver(o);
}

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

void* addEventObserver(const char *name, void *sender, std::function<void(const char *, void *, const EventParams&)> block) {
  addEventObserver(name, sender, new LambdaEventHandler(block));
}

void removeEventObserver(void *observer) {
  EventObserver *o = (EventObserver *)observer;
  _eventCenter->removeObserver(o);
  delete o;
}

void fireEvent(const char *name, void *sender, const EventParams& params) {
  _eventCenter->fire(name, sender, params);
}

// ---

static void setupEventCenter() {
  _eventCenter = new EventCenter;
}

static void teardownEventCenter() {
  delete _eventCenter;
  _eventCenter = nullptr;
}

Module moduleEventCenter = {
  .name     = "EventCenter",
  .setup    = setupEventCenter,
  .teardown = teardownEventCenter
};

}
