#pragma once
#include "envoy/tcp/factory.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {

/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
class DefaultTcpUpstreamFactory : public Envoy::Tcp::TcpUpstreamFactory {
public:
  ~DefaultTcpUpstreamFactory() override = default;
  Envoy::Tcp::ConnectionHandlePtr createTcpUpstreamHandle(
      Envoy::Upstream::ClusterManager& cluster_manager,
      Envoy::Upstream::LoadBalancerContext* lb_context,
      Envoy::Tcp::GenericUpstreamPoolCallbacks& generic_pool_callback,
      const std::shared_ptr<Envoy::Tcp::ConnectionPool::UpstreamCallbacks>& upstream_callback,
      absl::string_view hostname, const std::string& cluster_name) override;
};

} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy