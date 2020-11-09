#include "test/integration/upstreams/per_host_upstream_config.h"

#include "test/integration/upstreams/per_host_upstream_request.h"

namespace Envoy {

Router::GenericConnPoolPtr PerHostGenericConnPoolFactory::createGenericConnPool(
    Upstream::ClusterManager& cm, bool is_connect, const Router::RouteEntry& route_entry,
    absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  if (is_connect) {
    // This example factory doesn't support terminating CONNECT stream.
    return nullptr;
  }
  auto upstream_http_conn_pool =
      std::make_unique<PerHostHttpConnPool>(cm, is_connect, route_entry, downstream_protocol, ctx);
  return (upstream_http_conn_pool->valid() ? std::move(upstream_http_conn_pool) : nullptr);
}

} // namespace Envoy