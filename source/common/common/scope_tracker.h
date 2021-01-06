#pragma once

#include "envoy/common/scope_tracker.h"
#include "envoy/event/dispatcher.h"

#include "common/common/assert.h"

namespace Envoy {

// A small class for tracking the managing the scope of a tracked object which is currently having
// work done in this thread.
//
// When created, it appends the tracked object to the dispatcher's stack of tracked objects, and
// when destroyed it pops the dispatcher's stack of tracked object, which should be the object it
// registered.
class ScopeTrackerScopeState {
public:
  ScopeTrackerScopeState(const ScopeTrackedObject* object, Event::Dispatcher& dispatcher)
      : registered_object_(object), dispatcher_(dispatcher) {
    dispatcher_.appendTrackedObject(registered_object_);
  }

  ~ScopeTrackerScopeState() {
    // If ScopeTrackerScopeState is always used for managing tracked objects,
    // then the object popped off should be the object we registered.
    dispatcher_.popTrackedObject(registered_object_);
  }

  // Make this object stack-only, it doesn't make sense for it
  // to be on the heap since it's tracking a stack of active operations.
  void* operator new(std::size_t) = delete;

private:
  const ScopeTrackedObject* registered_object_;
  Event::Dispatcher& dispatcher_;
};

} // namespace Envoy
