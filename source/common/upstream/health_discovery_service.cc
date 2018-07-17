#include "common/upstream/health_discovery_service.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

HdsDelegate::HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
                         Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                         Runtime::Loader& runtime, Envoy::Stats::Store& stats,
                         Ssl::ContextManager& ssl_context_manager,
                         Secret::SecretManager& secret_manager, Runtime::RandomGenerator& random)
    : stats_{ALL_HDS_STATS(POOL_COUNTER_PREFIX(scope, "hds_delegate."))},
      async_client_(std::move(async_client)),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck")),
      runtime_(runtime), store_stats(stats), ssl_context_manager_(ssl_context_manager),
      secret_manager_(secret_manager), random_(random), dispatcher_(dispatcher) {
  health_check_request_.mutable_node()->MergeFrom(node);
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  server_response_timer_ = dispatcher.createTimer([this]() -> void { sendResponse(); });
  establishNewStream();
}

void HdsDelegate::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void HdsDelegate::setServerResponseTimer() {
  server_response_timer_->enableTimer(std::chrono::milliseconds(server_response_ms_));
}

void HdsDelegate::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  // TODO(lilika): Add support for other types of healthchecks
  health_check_request_.mutable_capability()->add_health_check_protocol(
      envoy::service::discovery::v2::Capability::HTTP);
  ENVOY_LOG(debug, "Sending HealthCheckRequest {} ", health_check_request_.DebugString());
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
}

// TODO(lilika) : Use jittered backoff as in https://github.com/envoyproxy/envoy/pull/3791
void HdsDelegate::handleFailure() {
  ENVOY_LOG(warn, "HdsDelegate stream/connection failure, will retry in {} ms.", RETRY_DELAY_MS);
  stats_.errors_.inc();
  setRetryTimer();
}

// TODO(lilika): Add support for the same endpoint in different clusters/ports
void HdsDelegate::sendResponse() {
  envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse response;
  for (uint32_t i = 0; i < hds_clusters_.size(); i++) {
    for (const auto& hosts : hds_clusters_[i]->prioritySet().hostSetsPerPriority()) {
      for (const auto& host : hosts->hosts()) {
        auto* endpoint = response.mutable_endpoint_health_response()->add_endpoints_health();
        endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
            host->address()->ip()->addressAsString());
        endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
            host->address()->ip()->port());
        if (host->healthy()) {
          endpoint->set_health_status(envoy::api::v2::core::HealthStatus::HEALTHY);
        } else {
          endpoint->set_health_status(envoy::api::v2::core::HealthStatus::UNHEALTHY);
        }
      }
    }
  }
  ENVOY_LOG(debug, "Sending EndpointHealthResponse to server {}", response.DebugString());
  stream_->sendMessage(response, false);
  stats_.responses_.inc();
  setServerResponseTimer();
}

void HdsDelegate::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::processMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {
  for (int i = 0; i < message->health_check_size(); i++) {
    // Create HdsCluster config
    ENVOY_LOG(debug, "Creating HdsCluster config");
    envoy::api::v2::core::BindConfig bind_config;
    envoy::api::v2::Cluster cluster_config;
    clusters_config_.push_back(cluster_config);
    clusters_config_[i].set_name(message->mutable_health_check(i)->cluster_name());
    clusters_config_[i].mutable_connect_timeout()->set_seconds(2);
    clusters_config_[i].mutable_http2_protocol_options();
    clusters_config_[i].mutable_per_connection_buffer_limit_bytes()->set_value(12345);

    for (int j = 0; j < message->mutable_health_check(i)->endpoints_size(); j++) {
      for (int k = 0; k < message->mutable_health_check(i)->mutable_endpoints(j)->endpoints_size();
           k++) {
        // Add endpoint to cluster
        clusters_config_[i].add_hosts()->MergeFrom(*message->mutable_health_check(i)
                                                    ->mutable_endpoints(j)
                                                    ->mutable_endpoints(k)
                                                    ->mutable_address());
      }
    }

    for (int j = 0; j < message->mutable_health_check(i)->health_checks_size(); j++) {
      // Add health checker to cluster
      clusters_config_[i].add_health_checks()->MergeFrom(
          *message->mutable_health_check(i)->mutable_health_checks(j));
    }

    ENVOY_LOG(debug, "New HdsCluster config {} ", clusters_config_[i].DebugString());

    // Creating HdsCluster
    cluster_.reset(new HdsCluster(runtime_, clusters_config_[i], bind_config, store_stats,
                                  ssl_context_manager_, secret_manager_, false));

    hds_clusters_.push_back(cluster_);
    std::vector<Upstream::HealthCheckerSharedPtr> cluster_health_checkers;
    for (int j = 0; j < message->mutable_health_check(i)->health_checks_size(); j++) {
      // Creating HealthCheckers
      cluster_health_checkers.push_back(Upstream::HealthCheckerFactory::create(
          *clusters_config_[i].mutable_health_checks(j), *hds_clusters_[i], runtime_, random_,
          dispatcher_));
    }
    health_checkers_ptr.push_back(cluster_health_checkers);
  }
}

// TODO(lilika): Add support for subsequent HealthCheckSpecifier messages that
// might modify the HdsClusters
void HdsDelegate::onReceiveMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {
  stats_.requests_.inc();
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());

  // Process the HealthCheckSpecifier message
  processMessage(std::move(message));

  // Initializing Clusters & HealthCheckers
  for (uint32_t i = 0; i < hds_clusters_.size(); i++) {
    for (uint32_t j = 0; j < health_checkers_ptr[i].size(); j++) {
      hds_clusters_[i]->setHealthChecker(health_checkers_ptr[i][j]);
    }
    hds_clusters_[i]->initialize([] {});
    server_response_ms_ = PROTOBUF_GET_MS_REQUIRED(*message, interval);
    setServerResponseTimer();
  }
}

void HdsDelegate::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
  server_response_timer_->disableTimer();
  stream_ = nullptr;
  handleFailure();
}

HdsCluster::HdsCluster(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                       const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager,
                       Secret::SecretManager& secret_manager, bool added_via_api)
    : info_(new ClusterInfoImpl(cluster, bind_config, runtime, stats, ssl_context_manager,
                                secret_manager, added_via_api)),
      runtime_(runtime), initial_hosts_(new HostVector())

{
  priority_set_.getOrCreateHostSet(0);

  for (const auto& host : cluster.hosts()) {
    initial_hosts_->emplace_back(HostSharedPtr{new HostImpl(
        info_, "", resolveProtoAddress2(host), envoy::api::v2::core::Metadata::default_instance(),
        1, envoy::api::v2::core::Locality().default_instance(),
        envoy::api::v2::endpoint::Endpoint::HealthCheckConfig().default_instance())});
  }
}

ClusterSharedPtr HdsCluster::create() { NOT_IMPLEMENTED; }

void HdsCluster::setHealthChecker(const HealthCheckerSharedPtr& health_checker) {
  ASSERT(!health_checker_);
  health_checker_ = health_checker;
  health_checker_->start();
}

HostVectorConstSharedPtr HdsCluster::createHealthyHostList(const HostVector& hosts) {
  HostVectorSharedPtr healthy_list(new HostVector());
  for (const auto& host : hosts) {
    if (host->healthy()) {
      healthy_list->emplace_back(host);
    }
  }
  return healthy_list;
}

HostsPerLocalityConstSharedPtr HdsCluster::createHealthyHostLists(const HostsPerLocality& hosts) {
  return hosts.filter([](const Host& host) { return host.healthy(); });
}

void HdsCluster::initialize(std::function<void()> callback) {
  ASSERT(initialization_complete_callback_ == nullptr);
  initialization_complete_callback_ = callback;
  if (health_checker_) {
    for (const auto& host : *initial_hosts_) {
      host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
  }

  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  auto healthy = createHealthyHostList(*initial_hosts_);
  ENVOY_LOG(debug, "About to update the priority set.");
  first_host_set.updateHosts(initial_hosts_, healthy, HostsPerLocalityImpl::empty(),
                             HostsPerLocalityImpl::empty(), {}, *initial_hosts_, {});
  initial_hosts_ = nullptr;
}

const Network::Address::InstanceConstSharedPtr
HdsCluster::resolveProtoAddress2(const envoy::api::v2::core::Address& address) {
  return Network::Address::resolveProtoAddress(address);
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector) {
  if (outlier_detector) {
    NOT_IMPLEMENTED;
  }
  NOT_IMPLEMENTED;
}

} // namespace Upstream
} // namespace Envoy
