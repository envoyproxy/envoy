#include "source/extensions/clusters/reverse_connection/reverse_connection.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

namespace BootstrapReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

// The default host header envoy expects when acting as a L4 proxy is of the format
// "<uuid>.tcpproxy.envoy.remote:<remote_port>".
const std::string default_proxy_host_suffix = "tcpproxy.envoy.remote";

absl::optional<absl::string_view>
RevConCluster::LoadBalancer::getUUIDFromHost(const Http::RequestHeaderMap& headers) {
  const absl::string_view original_host = headers.getHostValue();
  ENVOY_LOG(debug, "Host header value: {}", original_host);
  absl::string_view::size_type port_start = Http::HeaderUtility::getPortStart(original_host);
  if (port_start == absl::string_view::npos) {
    ENVOY_LOG(warn, "Port not found in host {}", original_host);
    port_start = original_host.size();
  } else {
    // Extract the port from the host header.
    const absl::string_view port_str = original_host.substr(port_start + 1);
    uint32_t port = 0;
    if (!absl::SimpleAtoi(port_str, &port)) {
      ENVOY_LOG(error, "Port {} is not valid", port_str);
      return absl::nullopt;
    }
  }
  // Extract the URI from the host header.
  const absl::string_view host = original_host.substr(0, port_start);
  const absl::string_view::size_type uuid_start = host.find('.');
  if (uuid_start == absl::string_view::npos ||
      host.substr(uuid_start + 1) != parent_->proxy_host_suffix_) {
    ENVOY_LOG(error,
              "Malformed host {} in host header {}. Expected: "
              "<node_uuid>.tcpproxy.envoy.remote:<remote_port>",
              host, original_host);
    return absl::nullopt;
  }
  return host.substr(0, uuid_start);
}

Upstream::HostSelectionResponse
RevConCluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (!context) {
    ENVOY_LOG(debug, "Invalid downstream connection or invalid downstream request");
    return {nullptr};
  }

  // First, Check if host_id is already set for the upstream cluster. If it is, use
  // that host_id.
  if (!parent_->default_host_id_.empty()) {
    return parent_->checkAndCreateHost(parent_->default_host_id_);
  }

  // Second, Check for the presence of headers in RevConClusterConfig's http_header_names in
  // the request context. In the absence of http_header_names in RevConClusterConfig, this
  // checks for the presence of EnvoyDstNodeUUID and EnvoyDstClusterUUID headers by default.
  if (context->downstreamHeaders() == nullptr) {
    ENVOY_LOG(error, "Found empty downstream headers for a request over connection with ID: {}",
              *(context->downstreamConnection()->connectionInfoProvider().connectionID()));
    return {nullptr};
  }

  // EnvoyDstClusterUUID is mandatory in each request. If this header is not
  // present, we will issue a malformed request error message.
  Http::HeaderMap::GetResult header_result =
      context->downstreamHeaders()->get(Http::Headers::get().EnvoyDstClusterUUID);
  if (header_result.empty()) {
    ENVOY_LOG(error, "{} header not found in request context",
              Http::Headers::get().EnvoyDstClusterUUID.get());
    return {nullptr};
  }
  const std::string host_id = std::string(parent_->getHostIdValue(context->downstreamHeaders()));
  if (host_id.empty()) {
    ENVOY_LOG(debug, "Found no header match for incoming request");
    return {nullptr};
  }

  // Finally, check the Host header for the UUID. This is mandatory if neither the host_id
  // nor any of the headers in RevConClusterConfig's http_header_names are set.
  absl::optional<absl::string_view> uuid = getUUIDFromHost(*context->downstreamHeaders());
  if (!uuid.has_value()) {
    ENVOY_LOG(error, "UUID not found in host header. Could not find host for request.");
    return {nullptr};
  }
  ENVOY_LOG(debug, "Found UUID in host header. Creating host with host_id: {}", uuid.value());
  return parent_->checkAndCreateHost(std::string(uuid.value()));
  
  // Get the SocketManager to resolve cluster ID to node ID
  auto* socket_manager = parent_->getUpstreamSocketManager();
  if (!socket_manager) {
    ENVOY_LOG(debug, "Socket manager not found");
    return {nullptr};
  }

  // Use SocketManager to resolve the key to a node ID
  std::string node_id = socket_manager->getNodeID(host_id);
  ENVOY_LOG(debug, "RevConCluster: Resolved key '{}' to node_id '{}'", host_id, node_id);
  
  return parent_->checkAndCreateHost(node_id);
}

Upstream::HostSelectionResponse RevConCluster::checkAndCreateHost(const std::string host_id) {
  host_map_lock_.ReaderLock();
  // Check if host_id is already present in host_map_ or not. This ensures,
  // that envoy reuses a conn_pool_container for an endpoint.
  auto host_itr = host_map_.find(host_id);
  if (host_itr != host_map_.end()) {
    ENVOY_LOG(debug, "Found an existing host for {}.", host_id);
    Upstream::HostSharedPtr host = host_itr->second;
    host_map_lock_.ReaderUnlock();
    return {host};
  }
  host_map_lock_.ReaderUnlock();

  absl::WriterMutexLock wlock(&host_map_lock_);

  // Create a custom address that uses the UpstreamReverseSocketInterface
  Network::Address::InstanceConstSharedPtr host_address(
      std::make_shared<UpstreamReverseConnectionAddress>(host_id));

  // Create a standard HostImpl using the custom address
  auto host_result = Upstream::HostImpl::create(
      info(), absl::StrCat(info()->name(), static_cast<std::string>(host_id)),
      std::move(host_address), nullptr /* endpoint_metadata */, nullptr /* locality_metadata */,
      1 /* initial_weight */, envoy::config::core::v3::Locality().default_instance(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(),
      0 /* priority */, envoy::config::core::v3::UNKNOWN);

  if (!host_result.ok()) {
    ENVOY_LOG(error, "Failed to create HostImpl for {}: {}", host_id,
              host_result.status().ToString());
    return {nullptr};
  }

  // Convert unique_ptr to shared_ptr
  Upstream::HostSharedPtr host(std::move(host_result.value()));
  // host->setHostId(host_id);
  ENVOY_LOG(trace, "Created a HostImpl {} for {} that will use UpstreamReverseSocketInterface.",
            *host, host_id);

  host_map_[host_id] = host;
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

absl::string_view RevConCluster::getHostIdValue(const Http::RequestHeaderMap* request_headers) {
  for (const auto& header_name : http_header_names_) {
    ENVOY_LOG(debug, "Searching for {} header in request context", header_name->get());
    Http::HeaderMap::GetResult header_result = request_headers->get(*header_name);
    if (header_result.empty()) {
      continue;
    }
    ENVOY_LOG(trace, "Found {} header in request context value {}", header_name->get(),
              header_result[0]->key().getStringView());
    // This is an implicitly untrusted header, so per the API documentation only the first
    // value is used.
    if (header_result[0]->value().empty()) {
      ENVOY_LOG(trace, "Found empty value for header {}", header_result[0]->key().getStringView());
      continue;
    }
    ENVOY_LOG(debug, "header_result value: {} ", header_result[0]->value().getStringView());
    return header_result[0]->value().getStringView();
  }

  return absl::string_view();
}

BootstrapReverseConnection::UpstreamSocketManager* RevConCluster::getUpstreamSocketManager() const {
  auto* upstream_interface = Network::socketInterface(
      "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
  if (!upstream_interface) {
    ENVOY_LOG(debug, "Upstream reverse socket interface not found");
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
    const envoy::extensions::clusters::reverse_connection::v3::RevConClusterConfig& rev_con_config)
    : ClusterImplBase(config, context, creation_status),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      cleanup_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(rev_con_config, cleanup_interval, 10000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })) {
  default_host_id_ =
      Config::Metadata::metadataValue(&config.metadata(), "envoy.reverse_conn", "host_id")
          .string_value();
  if (rev_con_config.proxy_host_suffix().empty()) {
    proxy_host_suffix_ = default_proxy_host_suffix;
  } else {
    proxy_host_suffix_ = rev_con_config.proxy_host_suffix();
  }
  // Parse HTTP header names.
  if (rev_con_config.http_header_names().size()) {
    for (const auto& header_name : rev_con_config.http_header_names()) {
      if (!header_name.empty()) {
        http_header_names_.emplace_back(Http::LowerCaseString(header_name));
      }
    }
  } else {
    http_header_names_.emplace_back(Http::Headers::get().EnvoyDstNodeUUID);
    http_header_names_.emplace_back(Http::Headers::get().EnvoyDstClusterUUID);
  }
  cleanup_timer_->enableTimer(cleanup_interval_);
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
RevConClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::reverse_connection::v3::RevConClusterConfig& proto_config,
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
