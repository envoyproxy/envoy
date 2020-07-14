#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/tcp/upstream_interface.h"

namespace Envoy {

namespace Upstream {
class LoadBalancerContext;
class ClusterManager;
} // namespace Upstream

namespace Tcp {

/*
 * A factory for creating generic connection pools.
 */
class GenericConnPoolFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~GenericConnPoolFactory() = default;
  virtual GenericConnPoolPtr createGenericConnPool(
      Envoy::Upstream::ClusterManager& cluster_manager, absl::string_view hostname,
      const std::string& cluster_name, Envoy::Upstream::LoadBalancerContext* lb_context,
      Envoy::Tcp::GenericUpstreamPoolCallbacks& generic_pool_callback,
      const std::shared_ptr<Envoy::Tcp::ConnectionPool::UpstreamCallbacks>& upstream_callback)
      const PURE;
};

using GenericConnPoolFactorySharedPtr = std::shared_ptr<GenericConnPoolFactory>;

} // namespace Tcp
} // namespace Envoy
