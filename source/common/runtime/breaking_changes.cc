#include "source/common/runtime/breaking_changes.h"

#include <optional>
#include <string>

#include "source/common/runtime/breaking_changes_flags.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"

#define FLAG_LOGGER(name)                                                                          \
  if (name) {                                                                                      \
    defects.push_back(#name);                                                                      \
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
  absl::InlinedVector<absl::string_view, 1> defects;
  ALL_BREAKING_CHANGES(FLAG_LOGGER, FLAG_LOGGER)

  if (defects.empty()) {
    return std::nullopt;
  }

  return absl::StrJoin(defects, ",");
}

} // namespace Runtime
} // namespace Envoy
