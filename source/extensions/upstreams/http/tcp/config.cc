#include "extensions/upstreams/http/tcp/config.h"

#include "extensions/upstreams/http/tcp/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

Router::GenericConnPoolPtr
TcpGenericConnPoolFactory::createGenericConnPool() const {
  return std::make_unique<Router::TcpConnPool>();
}

REGISTER_FACTORY(TcpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
