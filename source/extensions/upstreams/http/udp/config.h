#pragma once

#include "envoy/extensions/upstreams/http/udp/v3/udp_connection_pool.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Udp {

/**
 * Config registration for the UdpConnPool. @see Router::GenericConnPoolFactory
 */
class UdpGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.http.udp"; }
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr
  createGenericConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                        Router::GenericConnPoolFactory::UpstreamProtocol upstream_protocol,
                        Upstream::ResourcePriority priority,
                        absl::optional<Envoy::Http::Protocol> downstream_protocol,
                        Upstream::LoadBalancerContext* ctx) const override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::udp::v3::UdpConnectionPoolProto>();
  }
};

DECLARE_FACTORY(UdpGenericConnPoolFactory);

} // namespace Udp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
