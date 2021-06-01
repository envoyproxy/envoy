#pragma once

#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "extensions/internal_redirect/well_known_names.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class PreviousRoutesPredicate : public Router::InternalRedirectPredicate {
public:
  explicit PreviousRoutesPredicate(absl::string_view current_route_name)
      : current_route_name_(current_route_name) {}

  bool acceptTargetRoute(StreamInfo::FilterState& filter_state, absl::string_view route_name, bool,
                         bool) override;

  absl::string_view name() const override {
    return InternalRedirectPredicateValues::get().PreviousRoutesPredicate;
  }

private:
  const std::string current_route_name_;
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
