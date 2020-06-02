#include "extensions/upstreams/http/default/config.h"

#include "extensions/upstreams/http/http/upstream_request.h"
#include "extensions/upstreams/http/tcp/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Default {

Router::GenericConnPoolPtr
DefaultGenericConnPoolFactory::createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect, const Router::RouteEntry& route_entry,
                                                                       Envoy::Http::Protocol protocol, Upstream::LoadBalancerContext* ctx) const {
  if (is_connect) {
    auto ret = std::make_unique<Upstreams::Http::Tcp::TcpConnPool>(cm, is_connect, route_entry, protocol, ctx);
    return (ret->valid() ? std::move(ret) : nullptr);
  }
  auto ret = std::make_unique<Upstreams::Http::Http::HttpConnPool>(cm, is_connect, route_entry, protocol, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(DefaultGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Default
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
