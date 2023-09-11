#include "source/extensions/upstreams/http/generic/config.h"

#include "source/extensions/upstreams/http/http/upstream_request.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"
#ifdef ENVOY_ENABLE_QUIC
#include "source/extensions/upstreams/http/udp/upstream_request.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Generic {

using UpstreamProtocol = Envoy::Router::GenericConnPoolFactory::UpstreamProtocol;

Router::GenericConnPoolPtr GenericGenericConnPoolFactory::createGenericConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster, UpstreamProtocol upstream_protocol,
    const Router::RouteEntry& route_entry,
    absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  Router::GenericConnPoolPtr conn_pool;
  switch (upstream_protocol) {
  case UpstreamProtocol::HTTP:
    conn_pool = std::make_unique<Upstreams::Http::Http::HttpConnPool>(
        thread_local_cluster, route_entry, downstream_protocol, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
  case UpstreamProtocol::TCP:
    conn_pool =
        std::make_unique<Upstreams::Http::Tcp::TcpConnPool>(thread_local_cluster, route_entry, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
  case UpstreamProtocol::UDP:
#ifdef ENVOY_ENABLE_QUIC
    conn_pool = std::make_unique<Upstreams::Http::Udp::UdpConnPool>(thread_local_cluster, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
#else
    RELEASE_ASSERT(false, "UDP connection pool shouldn't be configured while QUIC code is compiled out.");
#endif
  }

  return nullptr;
}

REGISTER_FACTORY(GenericGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Generic
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
