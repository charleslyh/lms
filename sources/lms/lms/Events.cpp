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
  EventCenter() {
    mtx = SDL_CreateMutex();
  }
  
  ~EventCenter() {
    SDL_DestroyMutex(mtx);
  }
  
  void addObserver(EventObserver *o) {
    SDL_LockMutex(mtx);
    {
      observers.push_back(o);
    }
    SDL_UnlockMutex(mtx);
  }
  
  void removeObserver(EventObserver *o) {
    SDL_LockMutex(mtx);
    {
      observers.remove(o);
    }
    SDL_UnlockMutex(mtx);
  }
  
  void fire(const char *name, void *sender, const EventParams& params) {
    SDL_LockMutex(mtx);
    {
      for (auto o : observers) {
        if (strcmp(name, o->name) == 0 && (o->sender == nullptr || o->sender == sender)) {
          o->handler->handleEvent(name, sender, params);
        }
      }
    }
    SDL_UnlockMutex(mtx);
  }
  
private:
  std::list<EventObserver *> observers;
  SDL_mutex *mtx;
};

static EventCenter *_eventCenter;

void* addEventObserver(const char *name, void *sender, EventHandler *handler) {
  EventObserver *o = new EventObserver(name, sender, handler);
  _eventCenter->addObserver(o);
  return o;
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
  return addEventObserver(name, sender, new LambdaEventHandler(block));
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
