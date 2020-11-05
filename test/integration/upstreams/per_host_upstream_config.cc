#include "test/integration/upstreams/per_host_upstream_config.h"

#include "test/integration/upstreams/per_host_upstream_request.h"

namespace Envoy {

Router::GenericConnPoolPtr PerHostGenericConnPoolFactory::createGenericConnPool(
    Upstream::ClusterManager& cm, bool is_connect, const Router::RouteEntry& route_entry,
    absl::optional<Envoy::Http::Protocol> downstream_protocol,
    Upstream::LoadBalancerContext* ctx) const {
  if (is_connect) {
    return nullptr;
  }
  auto ret =
      std::make_unique<PerHostHttpConnPool>(cm, is_connect, route_entry, downstream_protocol, ctx);
  return (ret->valid() ? std::move(ret) : nullptr);
}

} // namespace Envoy