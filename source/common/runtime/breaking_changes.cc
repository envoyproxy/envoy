#include "source/common/runtime/breaking_changes.h"

#include <optional>
#include <string>

#include "source/common/runtime/breaking_changes_flags.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

// Global flag controlling whether observability for breaking changes is enabled.
// When disabled (the default), OBSERVED_BREAKING_CHANGE is a no-op to avoid filter state
// allocation overhead. When enabled, encountered breaking changes are recorded in the
// envoy.breaking_changes_tracker filter state object for access logging and monitoring.
ABSL_FLAG(bool, breaking_change_observability_enabled, false,
          "Enable observability for breaking changes.");

#define FLAG_LOGGER(name)                                                                          \
  if (name) {                                                                                      \
    changes.push_back(#name);                                                                      \
  }

namespace Envoy {
namespace Runtime {

BreakingChangesTracker&
BreakingChangesTracker::fromFilterState(const StreamInfo::FilterStateSharedPtr& filter_state,
                                        StreamInfo::FilterState::LifeSpan life_span) {
  auto* existing_obj =
      filter_state->getDataMutable<BreakingChangesTracker>(BreakingChangesTrackerDataName);
  if (existing_obj != nullptr) {
    return *existing_obj;
  }
  auto new_obj = std::make_shared<BreakingChangesTracker>();
  filter_state->setData(BreakingChangesTrackerDataName, new_obj, life_span);
  return *new_obj;
}

std::optional<std::string> BreakingChangesTracker::serializeAsString() const {
  // The likely case is that there are 0 defects. Sometimes there will be 1.
  // Very rarely, there will be 2.
  absl::InlinedVector<absl::string_view, 1> changes;
  ALL_BREAKING_CHANGES(FLAG_LOGGER, FLAG_LOGGER)

  if (changes.empty()) {
    return std::nullopt;
  }

  return absl::StrJoin(changes, ",");
}

} // namespace Runtime
} // namespace Envoy
