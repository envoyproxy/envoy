#include "source/extensions/upstreams/http/dynamic_modules/conn_pool.h"

#include "source/extensions/upstreams/http/dynamic_modules/http_tcp_bridge.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

DynamicModuleTcpConnPool::DynamicModuleTcpConnPool(
    DynamicModuleHttpTcpBridgeConfigSharedPtr config,
    Upstream::ThreadLocalCluster& thread_local_cluster, Upstream::ResourcePriority priority,
    Upstream::LoadBalancerContext* ctx)
    : config_(std::move(config)) {
  conn_pool_data_ = thread_local_cluster.tcpConnPool(priority, ctx);
}

void DynamicModuleTcpConnPool::newStream(Router::GenericConnectionPoolCallbacks* callbacks) {
  callbacks_ = callbacks;
  upstream_handle_ = conn_pool_data_.value().newConnection(*this);
}

bool DynamicModuleTcpConnPool::cancelAnyPendingStream() {
  if (upstream_handle_ != nullptr) {
    upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
    upstream_handle_ = nullptr;
    return true;
  }
  return false;
}

Upstream::HostDescriptionConstSharedPtr DynamicModuleTcpConnPool::host() const {
  return conn_pool_data_.value().host();
}

void DynamicModuleTcpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                             absl::string_view transport_failure_reason,
                                             Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onPoolFailure(reason, transport_failure_reason, host);
}

void DynamicModuleTcpConnPool::onPoolReady(
    Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
    Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();

  auto bridge = std::make_unique<DynamicModuleHttpTcpBridge>(
      config_, &callbacks_->upstreamToDownstream(), std::move(conn_data));

  ENVOY_LOG(debug, "dynamic module http-tcp bridge connection pool ready, host: {}",
            host->cluster().name());

  callbacks_->onPoolReady(std::move(bridge), host, latched_conn.connectionInfoProvider(),
                          latched_conn.streamInfo(), {});
}

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
