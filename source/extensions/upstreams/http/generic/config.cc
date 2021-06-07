#include "source/extensions/upstreams/http/generic/config.h"

#include "source/extensions/upstreams/http/http/upstream_request.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Generic {

Router::GenericConnPoolPtr GenericGenericConnPoolFactory::createGenericConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster, bool is_connect,
    const Router::RouteEntry& route_entry,
    absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  if (is_connect) {
    auto ret = std::make_unique<Upstreams::Http::Tcp::TcpConnPool>(
        thread_local_cluster, is_connect, route_entry, downstream_protocol, ctx);
    return (ret->valid() ? std::move(ret) : nullptr);
  }
  auto ret = std::make_unique<Upstreams::Http::Http::HttpConnPool>(
      thread_local_cluster, is_connect, route_entry, downstream_protocol, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

REGISTER_FACTORY(GenericGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Generic
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
