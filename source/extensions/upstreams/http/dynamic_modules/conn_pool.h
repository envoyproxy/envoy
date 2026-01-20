#pragma once

#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/common/logger.h"
#include "source/extensions/upstreams/http/dynamic_modules/bridge_config.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

/**
 * TCP connection pool for the dynamic module HTTP-TCP bridge.
 * This manages TCP connections to the upstream and creates DynamicModuleHttpTcpBridge
 * instances for each HTTP stream.
 */
class DynamicModuleTcpConnPool : public Router::GenericConnPool,
                                 public Envoy::Tcp::ConnectionPool::Callbacks,
                                 public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  DynamicModuleTcpConnPool(DynamicModuleHttpTcpBridgeConfigSharedPtr config,
                           Upstream::ThreadLocalCluster& thread_local_cluster,
                           Upstream::ResourcePriority priority, Upstream::LoadBalancerContext* ctx);

  // Router::GenericConnPool
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override;
  bool cancelAnyPendingStream() override;
  Upstream::HostDescriptionConstSharedPtr host() const override;
  bool valid() const override { return conn_pool_data_.has_value(); }

  // Envoy::Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  const DynamicModuleHttpTcpBridgeConfigSharedPtr config_;
  absl::optional<Envoy::Upstream::TcpPoolData> conn_pool_data_;
  Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle_{nullptr};
  Router::GenericConnectionPoolCallbacks* callbacks_{nullptr};
};

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
