#pragma once

#include "envoy/extensions/internal_redirect/whitelisted_routes/v3/whitelisted_routes_config.pb.h"
#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class WhitelistedRoutesPredicate : public Router::InternalRedirectPredicate {
public:
  WhitelistedRoutesPredicate(
      absl::string_view,
      const envoy::extensions::internal_redirect::whitelisted_routes::v3::WhitelistedRoutesConfig&
          config)
      : whitelisted_routes_(config.whitelisted_route_names().begin(),
                            config.whitelisted_route_names().end()) {}

  bool acceptTargetRoute(StreamInfo::FilterState&, absl::string_view route_name) {
    return whitelisted_routes_.contains(route_name);
  }

private:
  const absl::flat_hash_set<std::string> whitelisted_routes_;
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
