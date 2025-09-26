#include "source/extensions/clusters/reverse_connection/reverse_connection.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

namespace BootstrapReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

Upstream::HostSelectionResponse
RevConCluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (context == nullptr) {
    ENVOY_LOG(error, "reverse_connection: chooseHost called with null context");
    return {nullptr};
  }

  // Evaluate the configured host-id formatter to obtain the host identifier.
  if (context->downstreamHeaders() == nullptr) {
    ENVOY_LOG(error, "reverse_connection: missing downstream headers; cannot evaluate formatter.");
    return {nullptr};
  }

  // Format the host identifier using the configured formatter.
  const Envoy::Formatter::HttpFormatterContext formatter_context{
      context->downstreamHeaders(),     nullptr /* response_headers */,
      nullptr /* response_trailers */,  "" /* local_reply_body */,
      AccessLog::AccessLogType::NotSet, nullptr /* active_span */};

  const std::string host_id = parent_->host_id_formatter_->formatWithContext(
      formatter_context, context->downstreamConnection()->streamInfo());

  if (host_id.empty()) {
    ENVOY_LOG(error, "reverse_connection: host_id formatter returned empty value.");
    return {nullptr};
  }

  ENVOY_LOG(debug, "reverse_connection: using host identifier from formatter: {}", host_id);
  return parent_->checkAndCreateHost(host_id);
}

Upstream::HostSelectionResponse RevConCluster::checkAndCreateHost(absl::string_view host_id) {

  // Get the SocketManager to resolve cluster ID to node ID.
  auto* socket_manager = getUpstreamSocketManager();
  if (socket_manager == nullptr) {
    ENVOY_LOG(error,
              "reverse_connection: cannot create host for key: {}; socket manager not found.",
              host_id);
    return {nullptr};
  }

  // Use SocketManager to resolve the key to a node ID.
  std::string node_id = socket_manager->getNodeID(std::string(host_id));
  ENVOY_LOG(debug, "reverse_connection: resolved key '{}' to node: '{}'", host_id, node_id);

  host_map_lock_.ReaderLock();
  // Check if node_id is already present in host_map_ or not. This ensures,
  // that envoy reuses a conn_pool_container for an endpoint.
  auto host_itr = host_map_.find(node_id);
  if (host_itr != host_map_.end()) {
    ENVOY_LOG(debug, "reverse_connection: reusing existing host for {}.", node_id);
    Upstream::HostSharedPtr host = host_itr->second;
    host_map_lock_.ReaderUnlock();
    return {host};
  }
  host_map_lock_.ReaderUnlock();

  absl::WriterMutexLock wlock(&host_map_lock_);

  // Re-check under writer lock to avoid duplicate creation under contention.
  auto host_itr2 = host_map_.find(node_id);
  if (host_itr2 != host_map_.end()) {
    ENVOY_LOG(debug, "reverse_connection: host already created for {} during contention.", node_id);
    return {host_itr2->second};
  }

  // Create a custom address that uses the UpstreamReverseSocketInterface.
  Network::Address::InstanceConstSharedPtr host_address(
      std::make_shared<UpstreamReverseConnectionAddress>(node_id));

  // Create a standard HostImpl using the custom address.
  auto host_result = Upstream::HostImpl::create(
      info(), absl::StrCat(info()->name(), static_cast<std::string>(node_id)),
      std::move(host_address), nullptr /* endpoint_metadata */, nullptr /* locality_metadata */,
      1 /* initial_weight */, envoy::config::core::v3::Locality().default_instance(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(),
      0 /* priority */, envoy::config::core::v3::UNKNOWN);

  if (!host_result.ok()) {
    ENVOY_LOG(error, "reverse_connection: failed to create HostImpl for {}: {}", node_id,
              host_result.status().ToString());
    return {nullptr};
  }

  // Convert unique_ptr to shared_ptr.
  Upstream::HostSharedPtr host(std::move(host_result.value()));
  ENVOY_LOG(trace, "reverse_connection: created HostImpl {} for {}.", *host, node_id);

  host_map_[node_id] = host;
  return {host};
}

void RevConCluster::cleanup() {
  absl::WriterMutexLock wlock(&host_map_lock_);

  for (auto iter = host_map_.begin(); iter != host_map_.end();) {
    // Check if the host handle is acquired by any connection pool container or not. If not
    // clean those host to prevent memory leakage.
    const auto& host = iter->second;
    if (!host->used()) {
      ENVOY_LOG(debug, "Removing stale host: {}", *host);
      host_map_.erase(iter++);
    } else {
      ++iter;
    }
  }

  // Reschedule the cleanup after cleanup_interval_ duration.
  cleanup_timer_->enableTimer(cleanup_interval_);
}

BootstrapReverseConnection::UpstreamSocketManager* RevConCluster::getUpstreamSocketManager() const {
  auto* upstream_interface =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (upstream_interface == nullptr) {
    ENVOY_LOG(error, "Upstream reverse socket interface not found");
    return nullptr;
  }

  auto* upstream_socket_interface =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(upstream_interface);
  if (!upstream_socket_interface) {
    ENVOY_LOG(error, "Failed to cast to ReverseTunnelAcceptor");
    return nullptr;
  }

  auto* tls_registry = upstream_socket_interface->getLocalRegistry();
  if (!tls_registry) {
    ENVOY_LOG(error, "Thread local registry not found for upstream socket interface");
    return nullptr;
  }

  return tls_registry->socketManager();
}

RevConCluster::RevConCluster(
    const envoy::config::cluster::v3::Cluster& config, Upstream::ClusterFactoryContext& context,
    absl::Status& creation_status,
    const envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig&
        rev_con_config)
    : ClusterImplBase(config, context, creation_status),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      cleanup_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(rev_con_config, cleanup_interval, 60000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })) {
  // Create the host-id formatter from the format string.
  auto formatter_or_error =
      Envoy::Formatter::FormatterImpl::create(rev_con_config.host_id_format());
  if (!formatter_or_error.ok()) {
    creation_status = formatter_or_error.status();
    return;
  }
  host_id_formatter_ = std::move(*formatter_or_error);

  // Schedule periodic cleanup.
  cleanup_timer_->enableTimer(cleanup_interval_);
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
RevConClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig&
        proto_config,
    Upstream::ClusterFactoryContext& context) {
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return absl::InvalidArgumentError(
        fmt::format("cluster: LB policy {} is not valid for Cluster type {}. Only "
                    "'CLUSTER_PROVIDED' is allowed with cluster type 'REVERSE_CONNECTION'",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(cluster.lb_policy()),
                    cluster.cluster_type().name()));
  }

  if (cluster.has_load_assignment()) {
    return absl::InvalidArgumentError(
        "Reverse Conn clusters must have no load assignment configured");
  }

  absl::Status creation_status = absl::OkStatus();
  auto new_cluster = std::shared_ptr<RevConCluster>(
      new RevConCluster(cluster, context, creation_status, proto_config));
  RETURN_IF_NOT_OK(creation_status);
  auto lb = std::make_unique<RevConCluster::ThreadAwareLoadBalancer>(new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

/**
 * Static registration for the rev-con cluster factory. @see RegisterFactory.
 */
REGISTER_FACTORY(RevConClusterFactory, Upstream::ClusterFactory);

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
