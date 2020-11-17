#pragma once

#include "envoy/router/router.h"

#include "test/integration/upstreams/per_host_upstream_request.h"

namespace Envoy {

/**
 * Config registration for the HttpConnPool. @see Router::GenericConnPoolFactory
 */
class PerHostGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.http.per_host"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry,
                        absl::optional<Envoy::Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const override {
    if (is_connect) {
      // This example factory doesn't support terminating CONNECT stream.
      return nullptr;
    }
    auto upstream_http_conn_pool = std::make_unique<PerHostHttpConnPool>(
        cm, is_connect, route_entry, downstream_protocol, ctx);
    return (upstream_http_conn_pool->valid() ? std::move(upstream_http_conn_pool) : nullptr);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::Struct>();
  }
};

} // namespace Envoy