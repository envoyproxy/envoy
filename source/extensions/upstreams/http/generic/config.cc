#include "source/extensions/upstreams/http/generic/config.h"

#include "source/extensions/upstreams/http/http/upstream_request.h"
#include "source/extensions/upstreams/http/tcp/upstream_request.h"
#include "source/extensions/upstreams/http/udp/upstream_request.h"

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
  switch (upstream_protocol) {
  case UpstreamProtocol::HTTP: {
    auto http_conn_pool = std::make_unique<Upstreams::Http::Http::HttpConnPool>(
        thread_local_cluster, route_entry, downstream_protocol, ctx);
    return (http_conn_pool->valid() ? std::move(http_conn_pool) : nullptr);
  }
  case UpstreamProtocol::TCP: {
    auto tcp_conn_pool =
        std::make_unique<Upstreams::Http::Tcp::TcpConnPool>(thread_local_cluster, route_entry, ctx);
    return (tcp_conn_pool->valid() ? std::move(tcp_conn_pool) : nullptr);
  }
  case UpstreamProtocol::UDP: {
    auto udp_conn_pool =
        std::make_unique<Upstreams::Http::Udp::UdpConnPool>(thread_local_cluster, ctx);
    return (udp_conn_pool->valid() ? std::move(udp_conn_pool) : nullptr);
  }
  }

  return nullptr;
}

REGISTER_FACTORY(GenericGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Generic
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
