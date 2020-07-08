#pragma once
#include "common/tcp_proxy/factory.h"

namespace Envoy {
namespace TcpProxy {
/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
class DefaultTcpUpstreamFactory : public Envoy::TcpProxy::TcpUpstreamFactory {
public:
  ~DefaultTcpUpstreamFactory() override = default;
  Envoy::TcpProxy::ConnectionHandlePtr createTcpUpstreamHandle(
      Envoy::Upstream::ClusterManager& cluster_manager,
      Envoy::Upstream::LoadBalancerContext* lb_context,
      Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callback,
      const std::shared_ptr<Envoy::Tcp::ConnectionPool::UpstreamCallbacks>& upstream_callback,
      absl::string_view hostname, const std::string& cluster_name) override;
};

} // namespace TcpProxy
} // namespace Envoy
