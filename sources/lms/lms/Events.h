//
//  Events.hpp
//  lms
//
//  Created by yuhuachli on 2022/1/4.
//

#pragma once

#include <lms/Foundation.h>
#include <map>
#include <string>

namespace lms {

typedef std::map<std::string, Variant> EventParams;

class EventHandler : virtual public Object {
public:
  virtual void handleEvent(const char *name, void *sender, const EventParams& params) = 0;
};

typedef void (*EventCallback)(void *context, const char *eventName, void *sender, const EventParams& params);

void* addEventObserver(const char *name, void *sender, EventHandler *handler);
void* addEventObserver(const char *name, void *sender, std::function<void(const char *, void *, const EventParams&)> block);
void* addEventObserver(const char *name, void *sender, void *context, EventCallback evtCallback);
void removeEventObserver(void *observer);

void fireEvent(const char *name, void *sender, const EventParams& params = {});

}
