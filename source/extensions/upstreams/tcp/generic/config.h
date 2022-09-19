#pragma once

#include "envoy/extensions/upstreams/tcp/generic/v3/generic_connection_pool.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/tcp/upstream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Generic {

/**
 * Config registration for the GenericConnPool. * @see TcpProxy::GenericConnPoolFactory
 */
class GenericConnPoolFactory : public TcpProxy::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.filters.connection_pools.tcp.generic"; }
  std::string category() const override { return "envoy.upstreams"; }
  TcpProxy::GenericConnPoolPtr
  createGenericConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                        TcpProxy::TunnelingConfigHelperOptConstRef config,
                        Upstream::LoadBalancerContext* context,
                        Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                        StreamInfo::StreamInfo& downstream_info) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::upstreams::tcp::generic::v3::GenericConnectionPoolProto>();
  }
};

DECLARE_FACTORY(GenericConnPoolFactory);

} // namespace Generic
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
