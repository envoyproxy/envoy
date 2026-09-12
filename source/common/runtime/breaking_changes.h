#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/common/runtime/breaking_changes_flags.h"

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"

ABSL_DECLARE_FLAG(bool, envoy_reloadable_features_breaking_change_observability_enabled);

#define OBSERVED_BREAKING_CHANGE(name, filter_state)                                               \
  if (BreakingChangesTracker::IsEnabled()) {                                                       \
    BreakingChangesTracker::fromFilterState(filter_state).name = 1;                                \
  }

#define FEATURE_TRACKER(name) uint64_t name : 1 {0};

#define DECLARE_BREAKING_CHANGE(name) ABSL_DECLARE_FLAG(bool, envoy_reloadable_features_##name);

// Declare all breaking changes flags, so they can be accessed directly for efficiency.
ALL_BREAKING_CHANGES(DECLARE_BREAKING_CHANGE, DECLARE_BREAKING_CHANGE)

#define BREAKING_CHANGE_ENABLED(name) absl::GetFlag(FLAGS_envoy_reloadable_features_##name)

namespace Envoy {
namespace Runtime {

constexpr absl::string_view BreakingChangesTrackerDataName = "envoy.breaking_changes_tracker";

class BreakingChangesTracker : public StreamInfo::FilterState::Object {
public:
  BreakingChangesTracker() = default;

  static bool IsEnabled() {
    return absl::GetFlag(FLAGS_envoy_reloadable_features_breaking_change_observability_enabled);
  }

  static BreakingChangesTracker& fromFilterState(
      const StreamInfo::FilterStateSharedPtr& filter_state,
      StreamInfo::FilterState::LifeSpan life_span = StreamInfo::FilterState::LifeSpan::FilterChain);

  std::optional<std::string> serializeAsString() const override;

  // Flags tracking breaking changes.
  ALL_BREAKING_CHANGES(FEATURE_TRACKER, FEATURE_TRACKER)
};

} // namespace Runtime
} // namespace Envoy
