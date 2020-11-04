#pragma once

#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

namespace Envoy {

/**
 * Config registration for the HttpConnPool. @see Router::GenericConnPoolFactory
 */
class PerHostGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override {
    return "envoy.filters.connection_pools.http.per_host";
  }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ClusterManager& cm, bool is_connect,
                        const Router::RouteEntry& route_entry,
                        absl::optional<Envoy::Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return nullptr;
  }
};

DECLARE_FACTORY(PerHostGenericConnPoolFactory);


} // namespace Envoy