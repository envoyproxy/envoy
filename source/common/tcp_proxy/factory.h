#pragma once

#include "common/tcp_proxy/upstream_interface.h"

namespace Envoy {

namespace Upstream {
class LoadBalancerContext;
class ClusterManager;
} // namespace Upstream

namespace TcpProxy {

// TODO(lambdai): move to include path.
class TcpUpstreamFactory {
public:
  virtual ~TcpUpstreamFactory() = default;
  virtual ConnectionHandlePtr createTcpUpstreamHandle(
      Envoy::Upstream::ClusterManager& cluster_manager,
      Envoy::Upstream::LoadBalancerContext* lb_context,
      Envoy::TcpProxy::GenericUpstreamPoolCallbacks& generic_pool_callback,
      const std::shared_ptr<Envoy::Tcp::ConnectionPool::UpstreamCallbacks>& upstream_callback,
      absl::string_view hostname,
      // TODO(lambdai): Move to factory impl since cluster is the owner of this factory.
      const std::string& cluster_name) PURE;
};

using TcpUpstreamFactorySharedPtr = std::shared_ptr<TcpUpstreamFactory>;

} // namespace TcpProxy
} // namespace Envoy
