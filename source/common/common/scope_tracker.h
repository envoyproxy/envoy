#pragma once

#include "envoy/common/execution_context.h"
#include "envoy/common/scope_tracker.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/assert.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {

// A small class for managing the scope of a tracked object which is currently having
// work done in this thread.
//
// When created, it appends the tracked object to the tracker's stack of tracked objects, and
// when destroyed it pops the tracker's stack of tracked object, which should be the object it
// registered.
class ScopeTrackerScopeState {
public:
  ScopeTrackerScopeState(const ScopeTrackedObject* object, Event::ScopeTracker& tracker)
      : registered_object_(object), tracker_(tracker) {
    tracker_.pushTrackedObject(registered_object_);
  }

  ~ScopeTrackerScopeState() {
    // If ScopeTrackerScopeState is always used for managing tracked objects,
    // then the object popped off should be the object we registered.
    tracker_.popTrackedObject(registered_object_);
  }

  // Make this object stack-only, it doesn't make sense for it
  // to be on the heap since it's tracking a stack of active operations.
  void* operator new(std::size_t) = delete;

private:
  friend class ScopeTrackerScopeStateTest;

  const ScopeTrackedObject* registered_object_;
  Event::ScopeTracker& tracker_;

#ifdef ENVOY_ENABLE_EXECUTION_CONTEXT
  ScopedExecutionContext scoped_execution_context_{registered_object_};
#endif
};

} // namespace Envoy
