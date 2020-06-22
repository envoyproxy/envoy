#include "extensions/upstreams/http/tcp/config.h"

#include "common/router/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

Router::GenericConnPoolPtr TcpGenericConnPoolFactory::createGenericConnPool(
    Upstream::ClusterManager& cm, bool, const Router::RouteEntry& route_entry,
    Envoy::Http::Protocol protocol, Upstream::LoadBalancerContext* ctx) const {
  auto ret = std::make_unique<Router::TcpConnPool>(cm, route_entry, protocol, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(TcpGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
