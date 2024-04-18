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
    Upstream::ResourcePriority priority, absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  Router::GenericConnPoolPtr conn_pool;
  switch (upstream_protocol) {
  case UpstreamProtocol::HTTP:
    conn_pool = std::make_unique<Upstreams::Http::Http::HttpConnPool>(
        thread_local_cluster, priority, downstream_protocol, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
  case UpstreamProtocol::TCP:
    conn_pool =
        std::make_unique<Upstreams::Http::Tcp::TcpConnPool>(thread_local_cluster, priority, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
  case UpstreamProtocol::UDP:
    conn_pool = std::make_unique<Upstreams::Http::Udp::UdpConnPool>(thread_local_cluster, ctx);
    return (conn_pool->valid() ? std::move(conn_pool) : nullptr);
  }

  return nullptr;
}

REGISTER_FACTORY(GenericGenericConnPoolFactory, Router::GenericConnPoolFactory);

} // namespace Generic
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
