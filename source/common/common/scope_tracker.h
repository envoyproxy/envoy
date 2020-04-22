#pragma once

#include "envoy/common/scope_tracker.h"
#include "envoy/event/dispatcher.h"

namespace Envoy {

// A small class for tracking the scope of the object which is currently having
// work done in this thread.
//
// When created, it sets the tracked object in the dispatcher, and when destroyed it points the
// dispatcher at the previously tracked object.
class ScopeTrackerScopeState {
public:
  ScopeTrackerScopeState(const ScopeTrackedObject* object, Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {
    latched_object_ = dispatcher_.setTrackedObject(object);
  }

  ~ScopeTrackerScopeState() { dispatcher_.setTrackedObject(latched_object_); }

private:
  const ScopeTrackedObject* latched_object_;
  Event::Dispatcher& dispatcher_;
};

} // namespace Envoy
