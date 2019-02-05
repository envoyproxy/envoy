#include "common/router/scoped_config_impl.h"

#include "common/router/config_impl.h"

namespace Envoy {
namespace Router {

Router::ConfigConstSharedPtr ScopedConfigImpl::getRouterConfig(const Http::HeaderMap&) const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace Envoy
