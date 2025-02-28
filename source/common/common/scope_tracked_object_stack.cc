#include "source/common/common/scope_tracked_object_stack.h"

namespace Envoy {

void ScopeTrackedObjectStack::add(const ScopeTrackedObject& object) {
  tracked_objects_.push_back(object);
}

OptRef<const StreamInfo::StreamInfo> ScopeTrackedObjectStack::trackedStream() const {
  for (auto iter = tracked_objects_.rbegin(); iter != tracked_objects_.rend(); ++iter) {
    OptRef<const StreamInfo::StreamInfo> stream = iter->get().trackedStream();
    if (stream.has_value()) {
      return stream;
    }
  }
  return {};
}

void ScopeTrackedObjectStack::dumpState(std::ostream& os, int indent_level) const {
  for (auto iter = tracked_objects_.rbegin(); iter != tracked_objects_.rend(); ++iter) {
    iter->get().dumpState(os, indent_level);
  }
}

} // namespace Envoy
