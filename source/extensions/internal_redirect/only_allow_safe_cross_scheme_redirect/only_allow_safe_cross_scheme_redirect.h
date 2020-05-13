#pragma once

#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/internal_redirect/well_known_names.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class OnlyAllowSafeCrossSchemeRedirectPredicate : public Router::InternalRedirectPredicate {
public:
  bool acceptTargetRoute(StreamInfo::FilterState&, absl::string_view, bool downstream_is_https,
                         bool target_is_https) override {
    return downstream_is_https || !target_is_https;
  }

  absl::string_view name() const override {
    return InternalRedirectPredicateValues::get().OnlyAllowSafeCrossSchemeRedirectPredicate;
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
