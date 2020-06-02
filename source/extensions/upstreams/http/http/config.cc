#include "extensions/upstreams/http/http/config.h"

#include "extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

Router::GenericConnPoolPtr
HttpGenericConnPoolFactory::createGenericConnPool() const {
  return std::make_unique<HttpConnPool>();
}

REGISTER_FACTORY(HttpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
