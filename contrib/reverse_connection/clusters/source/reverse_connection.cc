#include "contrib/reverse_connection/clusters/source/reverse_connection.h"

#include <chrono>
#include <list>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

Upstream::HostConstSharedPtr RevConCluster::LoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (!context) {
    ENVOY_LOG(debug, "Invalid downstream connection or invalid downstream request");
    return nullptr;
  }

  // Check if host_id is already set for the upstream cluster. If it is, use
  // that host_id.
  if (!parent_->default_host_id_.empty()) {
    return parent_->checkAndCreateHost(absl::string_view(parent_->default_host_id_));
  }

  // Check if downstream headers are present, if yes use it to get host_id.
  if (context->downstreamHeaders() == nullptr) {
    ENVOY_LOG(error, "Found empty downstream headers for a request over connection with ID: {}",
              *(context->downstreamConnection()->connectionInfoProvider().connectionID()));
    return nullptr;
  }

  // EnvoyDstClusterUUID is mandatory in each request. If this header is not
  // present, we will issue a malformed request error message.
  Http::HeaderMap::GetResult header_result =
      context->downstreamHeaders()->get(Http::Headers::get().EnvoyDstClusterUUID);
  if (header_result.empty()) {
    ENVOY_LOG(error, "{} header not found in request context",
              Http::Headers::get().EnvoyDstClusterUUID.get());
    return nullptr;
  }
  absl::string_view host_id = parent_->getHostIdValue(context->downstreamHeaders());
  if (host_id.empty()) {
    ENVOY_LOG(debug, "Found no header match for incoming request");
    return nullptr;
  }
  return parent_->checkAndCreateHost(host_id);
}

Upstream::HostSharedPtr RevConCluster::checkAndCreateHost(const absl::string_view host_id) {
  host_map_lock_.ReaderLock();
  // Check if host_id is already present in host_map_ or not. This ensures,
  // that envoy reuses a conn_pool_container for an endpoint.
  auto host_itr = host_map_.find(host_id);
  if (host_itr != host_map_.end()) {
    ENVOY_LOG(debug, "Found an existing host for {}.", host_id);
    Upstream::HostSharedPtr host = host_itr->second;
    host_map_lock_.ReaderUnlock();
    return host;
  }
  host_map_lock_.ReaderUnlock();

  absl::WriterMutexLock wlock(&host_map_lock_);
  // We have to use genuine IPv4 address, otherwise Envoy will raise an exception
  // saying found malformed IPv4 address.
  Network::Address::InstanceConstSharedPtr host_ip_port(
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 0, nullptr));
  Upstream::HostSharedPtr host(std::make_shared<Upstream::HostImpl>(
      info(), absl::StrCat(info()->name(), static_cast<std::string>(host_id)),
      std::move(host_ip_port), nullptr /* metadata */, nullptr, 1 /* initial_weight */,
      envoy::config::core::v3::Locality().default_instance(),
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(),
      0 /* priority */, envoy::config::core::v3::UNKNOWN, time_source_));
  host->setHostId(host_id);
  ENVOY_LOG(trace, "Created a host {} for {}.", *host, host_id);

  host_map_[host_id] = host;
  return host;
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

RevConCluster::RevConCluster(const envoy::config::cluster::v3::Cluster& config,
                             Upstream::ClusterFactoryContext& context, absl::Status& creation_status,
                             const envoy::extensions::clusters::reverse_connection::v3alpha::RevConClusterConfig& rev_con_config)
    : ClusterImplBase(config, context, creation_status),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      cleanup_interval_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(rev_con_config, cleanup_interval, 10000))),
      cleanup_timer_(dispatcher_.createTimer([this]() -> void { cleanup(); })) {
  default_host_id_ =
      Config::Metadata::metadataValue(&config.metadata(), "envoy.reverse_conn", "host_id")
          .string_value();
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
RevConClusterFactory::createClusterImpl(const envoy::config::cluster::v3::Cluster& cluster,
                                        Upstream::ClusterFactoryContext& context) {
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return absl::InvalidArgumentError(
      fmt::format("cluster: LB policy {} is not valid for Cluster type {}. Only "
                    "'CLUSTER_PROVIDED' is allowed with cluster type 'REVERSE_CONNECTION'",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(cluster.lb_policy()),
                    envoy::config::cluster::v3::Cluster::DiscoveryType_Name(cluster.type())));
  }
  const auto& cluster_type = cluster.cluster_type();

  if (cluster_type.name() != "envoy.clusters.reverse_connection") {
    return absl::InvalidArgumentError(
        fmt::format("Invalid cluster type name: '{}', expected 'envoy.clusters.reverse_connection'",
                    cluster_type.name()));
  }

  if (cluster.has_load_assignment()) {
    return absl::InvalidArgumentError("Reverse Conn clusters must have no load assignment configured");
  }

  // Parse and validate the typed_config as RevConClusterConfig.
  envoy::extensions::clusters::reverse_connection::v3alpha::RevConClusterConfig rev_con_config;
  if (!cluster_type.typed_config().UnpackTo(&rev_con_config)) {
    return absl::InvalidArgumentError("Failed to unpack RevConClusterConfig");
  }

  absl::Status creation_status = absl::OkStatus();
  auto new_cluster =
      std::shared_ptr<RevConCluster>(new RevConCluster(cluster, context, creation_status, rev_con_config));
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
