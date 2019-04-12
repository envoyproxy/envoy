#include "common/router/scoped_config_manager.h"

#include "envoy/common/exception.h"

#include "common/common/fmt.h"

namespace Envoy {
namespace Router {

ScopedRouteInfoConstSharedPtr ScopedConfigManager::addOrUpdateRoutingScope(
    const envoy::api::v2::ScopedRouteConfiguration& config_proto, const std::string&) {
  auto scoped_route_info = std::make_shared<const ScopedRouteInfo>(config_proto);
  scoped_route_map_[config_proto.name()] = scoped_route_info;
  return scoped_route_info;
}

bool ScopedConfigManager::removeRoutingScope(const std::string& name) {
  if (scoped_route_map_.erase(name) == 0) {
    throw EnvoyException(fmt::format("could not find {} in scoped route map for removal", name));
  }
  return true;
}

} // namespace Router
} // namespace Envoy
