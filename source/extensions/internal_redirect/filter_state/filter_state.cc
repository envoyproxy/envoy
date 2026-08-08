#include "source/extensions/internal_redirect/filter_state/filter_state.h"

#include "envoy/stream_info/bool_accessor.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

bool FilterStatePredicate::acceptTargetRoute(StreamInfo::FilterState& filter_state,
                                             absl::string_view, bool, bool) {
  const auto* accessor =
      filter_state.getDataReadOnly<StreamInfo::BoolAccessor>(redirect_enabled_key_);
  if (accessor == nullptr) {
    // No gate signal was set by any upstream/downstream filter.
    return redirect_if_absent_;
  }
  // Simply return the boolean value: true = follow redirect, false = don't follow.
  return accessor->value();
}

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
