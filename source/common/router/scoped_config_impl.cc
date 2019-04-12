#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

void ThreadLocalScopedConfigImpl::addOrUpdateRoutingScope(ScopedRouteInfoConstSharedPtr) {}

void ThreadLocalScopedConfigImpl::removeRoutingScope(const std::string&) {}

Router::ConfigConstSharedPtr
ThreadLocalScopedConfigImpl::getRouterConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
