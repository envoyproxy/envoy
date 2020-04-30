#pragma once

#include "envoy/extensions/internal_redirect/allow_listed_routes/v3/allow_listed_routes_config.pb.h"
#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class AllowListedRoutesPredicate : public Router::InternalRedirectPredicate {
public:
  AllowListedRoutesPredicate(
      absl::string_view,
      const envoy::extensions::internal_redirect::allow_listed_routes::v3::AllowListedRoutesConfig&
          config)
      : allowed_routes_(config.allowed_route_names().begin(), config.allowed_route_names().end()) {}

  bool acceptTargetRoute(StreamInfo::FilterState&, absl::string_view route_name) override {
    return allowed_routes_.contains(route_name);
  }

private:
  const absl::flat_hash_set<std::string> allowed_routes_;
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
