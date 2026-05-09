#pragma once

#include <functional>
#include <ranges>
#include <vector>

#include "envoy/common/execution_context.h"
#include "envoy/common/scope_tracker.h"

namespace Envoy {

// Encapsulates zero or more ScopeTrackedObjects.
//
// This is currently used to restore the underlying request context if a
// filter continues processing a request or response due to being invoked directly from an
// asynchronous callback.
class ScopeTrackedObjectStack : public ScopeTrackedObject {
public:
  ScopeTrackedObjectStack() = default;

  // Not copyable or movable
  ScopeTrackedObjectStack(const ScopeTrackedObjectStack&) = delete;
  ScopeTrackedObjectStack& operator=(const ScopeTrackedObjectStack&) = delete;

  void add(const ScopeTrackedObject& object) { tracked_objects_.push_back(object); }

  OptRef<const StreamInfo::StreamInfo> trackedStream() const override {
    for (const auto& tracked_object : std::ranges::reverse_view(tracked_objects_)) {
      OptRef<const StreamInfo::StreamInfo> stream = tracked_object.get().trackedStream();
      if (stream.has_value()) {
        return stream;
      }
    }
    return {};
  }

  void dumpState(std::ostream& os, int indent_level) const override {
    for (const auto& tracked_object : std::ranges::reverse_view(tracked_objects_)) {
      tracked_object.get().dumpState(os, indent_level);
    }
  }

private:
  std::vector<std::reference_wrapper<const ScopeTrackedObject>> tracked_objects_;
};

} // namespace Envoy
