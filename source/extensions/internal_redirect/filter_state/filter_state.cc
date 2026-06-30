#include "source/extensions/internal_redirect/filter_state/filter_state.h"

#include "envoy/router/string_accessor.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

bool FilterStatePredicate::acceptTargetRoute(StreamInfo::FilterState& filter_state,
                                             absl::string_view, bool, bool) {
  const auto* accessor = filter_state.getDataReadOnly<Router::StringAccessor>(filter_state_key_);
  if (accessor == nullptr) {
    // No gate signal was set by any upstream/downstream filter.
    return allow_if_absent_;
  }
  const bool matched = accessor->asString() == compare_value_;
  // Follow iff the match outcome is the one we follow on: allow_value follows on
  // a match (follow_on_match_=true), deny_value follows on a non-match.
  return matched == follow_on_match_;
}

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
