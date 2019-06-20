#pragma once

#include "envoy/common/scope_tracker.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {

class ScopeTrackerImpl {
public:
  ScopeTrackerImpl(const ScopeTrackedObject* object, Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {
    latched_object_ = dispatcher_.setTrackedObject(object);
  }

  ~ScopeTrackerImpl() { dispatcher_.setTrackedObject(latched_object_); }

private:
  const ScopeTrackedObject* latched_object_;
  Event::Dispatcher& dispatcher_;
};

} // namespace Envoy
