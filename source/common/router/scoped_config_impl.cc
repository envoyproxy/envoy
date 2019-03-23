#include "common/router/scoped_config_impl.h"

#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

ScopedRouteInfoConstSharedPtr
ScopedConfigManager::addOrUpdateRoutingScope(const envoy::api::v2::ScopedRouteConfiguration&,
                                             const std::string&) {
  return std::make_shared<ScopedRouteInfo>();
}

bool ScopedConfigManager::removeRoutingScope(const std::string&) { return true; }

void ThreadLocalScopedConfigImpl::addOrUpdateRoutingScope(ScopedRouteInfoConstSharedPtr) {}

void ThreadLocalScopedConfigImpl::removeRoutingScope(const std::string&) {}

Router::ConfigConstSharedPtr
ThreadLocalScopedConfigImpl::getRouterConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
