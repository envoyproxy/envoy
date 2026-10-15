#include "source/extensions/internal_redirect/filter_state/filter_state.h"

#include "envoy/stream_info/bool_accessor.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

bool FilterStatePredicate::acceptTargetRoute(StreamInfo::FilterState& filter_state,
                                             absl::string_view, bool, bool) {
  const auto* object = filter_state.getDataReadOnlyGeneric(redirect_enabled_key_);
  if (object == nullptr) {
    return redirect_if_absent_;
  }

  const auto* accessor = dynamic_cast<const StreamInfo::BoolAccessor*>(object);
  if (accessor == nullptr) {
    return false;
  }

  return accessor->value();
}

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
