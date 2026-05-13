#include "source/extensions/clusters/reverse_connection/reverse_connection.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/fmt.h"
#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

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
  const Envoy::Formatter::Context formatter_context{
      context->downstreamHeaders(),     nullptr /* response_headers */,
      nullptr /* response_trailers */,  "" /* local_reply_body */,
      AccessLog::AccessLogType::NotSet, nullptr /* active_span */};

  // Use request stream info if available, otherwise fall back to connection stream info.
  const StreamInfo::StreamInfo& stream_info = context->requestStreamInfo()
                                                  ? *context->requestStreamInfo()
                                                  : context->downstreamConnection()->streamInfo();

  const std::string host_id = parent_->host_id_formatter_->format(formatter_context, stream_info);

  // Treat "-" (formatter default for missing) as empty as well.
  if (host_id.empty() || host_id == "-") {
    ENVOY_LOG(error, "reverse_connection: host_id formatter returned empty value.");
    return {nullptr};
  }

  // Check if tenant isolation is enabled and tenant_id_formatter is configured.
  std::string final_host_id = host_id;
  auto* socket_manager = parent_->getUpstreamSocketManager();
  if (socket_manager != nullptr && socket_manager->tenantIsolationEnabled()) {
    // When tenant isolation is enabled, tenant_id_formatter must be configured.
    if (parent_->tenant_id_formatter_ == nullptr) {
      ENVOY_LOG(error,
                "reverse_connection: tenant isolation is enabled but tenant_id_format is not "
                "configured. tenant_id_format is required when tenant isolation is enabled.");
      return {nullptr};
    }
    // Format tenant identifier.
    const std::string tenant_id =
        parent_->tenant_id_formatter_->format(formatter_context, stream_info);

    // Treat "-" (formatter default for missing) as empty as well.
    if (!tenant_id.empty() && tenant_id != "-") {
      // Concatenate tenant_id and host_id using the utility function.
      final_host_id =
          BootstrapReverseConnection::ReverseConnectionUtility::buildTenantScopedIdentifier(
              tenant_id, host_id);
      ENVOY_LOG(debug,
                "reverse_connection: tenant isolation enabled, using tenant-scoped identifier: {}",
                final_host_id);
    } else {
      // When tenant isolation is enabled, tenant_id must be derivable.
      ENVOY_LOG(error,
                "reverse_connection: tenant isolation enabled but tenant_id cannot be inferred "
                "(formatter returned empty value)");
      return {nullptr};
    }
  }

  ENVOY_LOG(debug, "reverse_connection: using host identifier: {}", final_host_id);

  HostLookupResult lookup = parent_->checkAndCreateHost(final_host_id);

  if (lookup.newly_created) {
    // Mutate priority_set_ on the main dispatcher, where cleanup() also runs.
    Upstream::HostSharedPtr host = lookup.host;
    std::weak_ptr<RevConCluster> weak_parent = parent_;
    parent_->dispatcher_.post(
        [weak_parent, host = std::move(host)]() mutable {
          if (auto parent = weak_parent.lock()) {
            parent->addHostToHostSet(std::move(host));
          }
        });
  }

  return Upstream::HostSelectionResponse{lookup.host};
}

RevConCluster::HostLookupResult RevConCluster::checkAndCreateHost(absl::string_view host_id) {
  // Get the SocketManager to resolve cluster ID to node ID.
  // The bootstrap extension is validated during cluster creation, and TLS is initialized before
  // request handling, so socket_manager should always be available.
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT(socket_manager != nullptr, "Socket manager should be initialized before request handling");

  // Use SocketManager to resolve the key to a node ID.
  std::string node_id = socket_manager->getNodeWithSocket(std::string(host_id));
  ENVOY_LOG(debug, "reverse_connection: resolved key '{}' to node: '{}'", host_id, node_id);

  {
    absl::ReaderMutexLock rlock(host_map_lock_);
    auto host_itr = host_map_.find(node_id);
    if (host_itr != host_map_.end()) {
      ENVOY_LOG(debug, "reverse_connection: reusing existing host for {}.", node_id);
      // Keep the host alive for at least one cleanup interval after selection.
      host_itr->second.used.store(true, std::memory_order_relaxed);
      return {host_itr->second.host, /*newly_created=*/false};
    }
  }

  absl::WriterMutexLock wlock(host_map_lock_);

  // Re-check under writer lock to avoid duplicate creation under contention.
  auto host_itr2 = host_map_.find(node_id);
  if (host_itr2 != host_map_.end()) {
    ENVOY_LOG(debug, "reverse_connection: host already created for {} during contention.", node_id);
    host_itr2->second.used.store(true, std::memory_order_relaxed);
    return {host_itr2->second.host, /*newly_created=*/false};
  }

  // Create a custom address that uses the UpstreamReverseSocketInterface.
  Network::Address::InstanceConstSharedPtr host_address(
      std::make_shared<UpstreamReverseConnectionAddress>(node_id));

  // Create a standard HostImpl using the custom address.
  auto host_result = Upstream::HostImpl::create(
      info(), absl::StrCat(info()->name(), static_cast<std::string>(node_id)),
      std::move(host_address), nullptr /* endpoint_metadata */, nullptr /* locality_metadata */,
      1 /* initial_weight */, std::make_shared<const envoy::config::core::v3::Locality>(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(),
      0 /* priority */, envoy::config::core::v3::UNKNOWN);

  // Convert unique_ptr to shared_ptr.
  Upstream::HostSharedPtr host(std::move(host_result.value()));
  ENVOY_LOG(trace, "reverse_connection: created HostImpl {} for {}.", *host, node_id);

  auto [it, inserted] = host_map_.try_emplace(node_id, host);
  ASSERT(inserted, "host_map_ entry already existed despite re-check under writer lock");
  return {it->second.host, /*newly_created=*/true};
}

void RevConCluster::addHostToHostSet(Upstream::HostSharedPtr host) {
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  auto all_hosts = std::make_shared<Upstream::HostVector>(first_host_set.hosts());
  all_hosts->emplace_back(host);
  ENVOY_LOG(debug, "reverse_connection: adding host to priority set, total hosts: {}",
            all_hosts->size());
  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, Upstream::HostsPerLocalityImpl::empty()),
      {}, {std::move(host)}, {}, /*weighted_priority_health=*/false, absl::nullopt);
}

void RevConCluster::cleanup() {
  Upstream::HostVector to_be_removed;
  Upstream::HostVectorSharedPtr keeping_hosts;

  {
    absl::WriterMutexLock wlock(host_map_lock_);
    keeping_hosts = std::make_shared<Upstream::HostVector>();

    for (auto iter = host_map_.begin(); iter != host_map_.end();) {
      auto& entry = iter->second;
      if (entry.used.load(std::memory_order_relaxed)) {
        entry.used.store(false, std::memory_order_relaxed);
        keeping_hosts->push_back(entry.host);
        ++iter;
      } else if (entry.host->used()) {
        keeping_hosts->push_back(entry.host);
        ++iter;
      } else {
        ENVOY_LOG(debug, "Removing stale host: {}", *entry.host);
        to_be_removed.push_back(entry.host);
        host_map_.erase(iter++);
      }
    }
  }

  if (!to_be_removed.empty()) {
    ENVOY_LOG(debug,
              "reverse_connection: cleaned up {} stale hosts from priority set, remaining: {}",
              to_be_removed.size(), keeping_hosts->size());
    priority_set_.updateHosts(0,
                              Upstream::HostSetImpl::partitionHosts(
                                  keeping_hosts, Upstream::HostsPerLocalityImpl::empty()),
                              {}, {}, to_be_removed, /*weighted_priority_health=*/false,
                              absl::nullopt);
  }

  cleanup_timer_->enableTimer(cleanup_interval_);
}

BootstrapReverseConnection::UpstreamSocketManager* RevConCluster::getUpstreamSocketManager() const {
  auto* upstream_interface =
      Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  ASSERT(upstream_interface != nullptr,
         "Upstream reverse socket interface should be validated during cluster creation");

  auto* upstream_socket_interface =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(upstream_interface);
  ASSERT(upstream_socket_interface != nullptr,
         "Socket interface type should be validated during cluster creation");

  // TLS is initialized in onServerInitialized() which is called after cluster creation but before
  // request handling, so it should always be available when this method is called.
  auto* tls_registry = upstream_socket_interface->getLocalRegistry();
  ASSERT(tls_registry != nullptr,
         "TLS should be initialized by onServerInitialized() before request handling");

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
  auto formatter_or_error = Envoy::Formatter::FormatterImpl::create(
      rev_con_config.host_id_format(), /*omit_empty_values=*/false,
      Envoy::Formatter::BuiltInCommandParserFactoryHelper::commandParsers());
  host_id_formatter_ = std::move(*formatter_or_error);

  // Create the tenant-id formatter if configured.
  if (!rev_con_config.tenant_id_format().empty()) {
    auto tenant_formatter_or_error = Envoy::Formatter::FormatterImpl::create(
        rev_con_config.tenant_id_format(), /*omit_empty_values=*/false,
        Envoy::Formatter::BuiltInCommandParserFactoryHelper::commandParsers());
    tenant_id_formatter_ = std::move(*tenant_formatter_or_error);
  }

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

  // Validate that the required bootstrap extension is configured using Envoy's standard utility.
  const std::string extension_name = "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
  auto* factory =
      Config::Utility::getAndCheckFactoryByName<Server::Configuration::BootstrapExtensionFactory>(
          extension_name, /*is_optional=*/true);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Reverse connection cluster requires the upstream reverse tunnel bootstrap extension '{}' "
        "to be configured. Please add it to bootstrap_extensions in your bootstrap configuration.",
        extension_name));
  }

  // Validate that the factory is a ReverseTunnelAcceptor.
  auto* upstream_socket_interface =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(factory);
  if (upstream_socket_interface == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("Bootstrap extension '{}' exists but is not of the expected type "
                    "(ReverseTunnelAcceptor). This indicates a configuration error.",
                    extension_name));
  }

  // Validate that if tenant isolation is enabled in bootstrap config, tenant_id_format is
  // configured.
  auto* extension = upstream_socket_interface->getExtension();
  if (extension != nullptr && extension->enableTenantIsolation() &&
      proto_config.tenant_id_format().empty()) {
    return absl::InvalidArgumentError(
        fmt::format("tenant_id_format must be configured for reverse connection cluster '{}' when "
                    "tenant isolation is enabled in the bootstrap configuration. Please configure "
                    "tenant_id_format in the reverse connection cluster configuration.",
                    cluster.name()));
  }

  // Validate the host_id_format early to catch formatter errors.
  auto validation_or_error = Envoy::Formatter::FormatterImpl::create(
      proto_config.host_id_format(), /*omit_empty_values=*/false,
      Envoy::Formatter::BuiltInCommandParserFactoryHelper::commandParsers());
  RETURN_IF_NOT_OK_REF(validation_or_error.status());

  // Validate the tenant_id_format if provided.
  if (!proto_config.tenant_id_format().empty()) {
    auto tenant_validation_or_error = Envoy::Formatter::FormatterImpl::create(
        proto_config.tenant_id_format(), /*omit_empty_values=*/false,
        Envoy::Formatter::BuiltInCommandParserFactoryHelper::commandParsers());
    RETURN_IF_NOT_OK_REF(tenant_validation_or_error.status());
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
