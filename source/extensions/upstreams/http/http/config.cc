#include "extensions/upstreams/http/http/config.h"

#include "extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {

Router::GenericConnPoolPtr
HttpGenericConnPoolFactory::createGenericConnPool(HttpOrTcpPool pool) const {
  return std::make_unique<HttpConnPool>(*absl::get<Envoy::Http::ConnectionPool::Instance*>(pool));
}

REGISTER_FACTORY(HttpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
