#pragma once

#include "envoy/extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/v3/only_allow_safe_cross_scheme_redirect_config.pb.h"
#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/internal_redirect/well_known_names.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class OnlyAllowSafeCrossSchemeRedirectPredicate : public Router::InternalRedirectPredicate {
public:
  OnlyAllowSafeCrossSchemeRedirectPredicate() {}

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
