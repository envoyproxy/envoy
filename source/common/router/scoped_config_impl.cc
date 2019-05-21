#include "common/router/scoped_config_impl.h"

namespace Envoy {
namespace Router {

void ThreadLocalScopedConfigImpl::addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr&) {}

void ThreadLocalScopedConfigImpl::removeRoutingScope(const std::string&) {}

Router::ConfigConstSharedPtr
ThreadLocalScopedConfigImpl::getRouteConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
