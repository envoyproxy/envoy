#pragma once

#include <functional>
#include <initializer_list>

#include "envoy/common/scope_tracker.h"

#include "common/common/assert.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {

// A small class for encapsulating 1 or more scope tracked objects.
//
// This is currently used to restore the underlying request context if the
// filter continues processing a request due to a callback that it had posted.
class OpaqueScopeTrackedObject : public ScopeTrackedObject {
public:
  OpaqueScopeTrackedObject(
      std::vector<std::reference_wrapper<const ScopeTrackedObject>>&& tracked_objects)
      : tracked_objects_(tracked_objects) {
    ASSERT(!tracked_objects_.empty());
  }

  ~OpaqueScopeTrackedObject() = default;

  void dumpState(std::ostream& os, int indent_level) const override {
    for (auto iter = tracked_objects_.rbegin(); iter != tracked_objects_.rend(); ++iter) {
      iter->get().dumpState(os, indent_level);
    }
  }

private:
  std::vector<std::reference_wrapper<const ScopeTrackedObject>> tracked_objects_;
};

} // namespace Envoy
