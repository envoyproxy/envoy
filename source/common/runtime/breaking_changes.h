#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/common/runtime/breaking_changes_flags.h"

#include "absl/strings/string_view.h"

#define RECORD_BREAKING_CHANGE(name, filter_state)                                                 \
  BreakingChangesTracker::fromFilterState(filter_state).name = 1;

#define FEATURE_TRACKER(name) uint64_t name : 1 {0};
#define FEATURE_TRACKER(name) uint64_t name : 1 {0};

namespace Envoy {
namespace Runtime {

constexpr absl::string_view BreakingChangesTrackerDataName = "envoy.breaking_changes_tracker";

class BreakingChangesTracker : public StreamInfo::FilterState::Object {
public:
  BreakingChangesTracker() = default;

  static BreakingChangesTracker& fromFilterState(
      const StreamInfo::FilterStateSharedPtr& filter_state,
      StreamInfo::FilterState::LifeSpan life_span = StreamInfo::FilterState::LifeSpan::FilterChain);

  std::optional<std::string> serializeAsString() const override;

  // Flags tracking breaking changes.
  ALL_BREAKING_CHANGES(FEATURE_TRACKER, FEATURE_TRACKER)
};

} // namespace Runtime
} // namespace Envoy
