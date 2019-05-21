#pragma once

#include <map>
#include <string>

#include "envoy/api/v2/srds.pb.h"

namespace Envoy {
namespace Router {

// The internal representation of the configuration distributed via the ScopedRouteConfiguration
// proto.
class ScopedRouteInfo {
public:
  ScopedRouteInfo(const envoy::api::v2::ScopedRouteConfiguration& config_proto)
      : config_proto_(config_proto) {}

  // TODO(AndresGuedez): Add the necessary APIs required for the scoped routing logic.

  const envoy::api::v2::ScopedRouteConfiguration config_proto_;
};
using ScopedRouteInfoConstSharedPtr = std::shared_ptr<const ScopedRouteInfo>;

// A manager for routing configuration scopes.
// An instance of the manager is owned by each ScopedRdsConfigSubscription. When config updates are
// received (on the main thread), the manager is called to track changes to the set of scoped route
// configurations and build <ScopedRouteInfo>s as needed.
class ScopedConfigManager {
public:
  // Ordered map for consistent config dumping.
  using ScopedRouteMap = std::map<std::string, ScopedRouteInfoConstSharedPtr>;

  // Adds/updates a routing scope specified via the Scoped RDS API. This scope will be added to the
  // set of scopes matched against the scope keys built for each HTTP request.
  ScopedRouteInfoConstSharedPtr
  addOrUpdateRoutingScope(const envoy::api::v2::ScopedRouteConfiguration& scoped_route_config,
                          const std::string& version_info);

  // Removes a routing scope from the set of scopes matched against each HTTP request.
  bool removeRoutingScope(const std::string& scope_name);

  const ScopedRouteMap& scopedRouteMap() const { return scoped_route_map_; }

private:
  ScopedRouteMap scoped_route_map_;
};

} // namespace Router
} // namespace Envoy
