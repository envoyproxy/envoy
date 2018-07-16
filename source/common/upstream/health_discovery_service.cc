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
  response_timer_ = dispatcher.createTimer([this]() -> void { sendHealthCheckRequest(); });
  server_responce_timer_ = dispatcher.createTimer([this]() -> void { sendResponse(); });
  establishNewStream();
}

void HdsDelegate::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
}

void HdsDelegate::setServerResponseTimer() {
  server_responce_timer_->enableTimer(std::chrono::seconds(SERVER_RESPONSE_S));
}

void HdsDelegate::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this);
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  sendHealthCheckRequest();
}

void HdsDelegate::sendHealthCheckRequest() {
  ENVOY_LOG(debug, "Sending HealthCheckRequest");
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
}

// TODO(lilika) : Use jittered backoff as in https://github.com/envoyproxy/envoy/pull/3791
void HdsDelegate::handleFailure() {
  ENVOY_LOG(warn, "HdsDelegate stream/connection failure, will retry in {} ms.", RETRY_DELAY_MS);
  stats_.errors_.inc();
  setRetryTimer();
}

void HdsDelegate::sendResponse() {
  envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse response;
  for (uint i = 0; i < hds_clusters_.size(); i++) {
    for (const auto& hosts : hds_clusters_[i]->prioritySet().hostSetsPerPriority()) {
      for (const auto& host : hosts->hosts()) {
        auto* endpoint = response.mutable_endpoint_health_response()->add_endpoints_health();
        auto address = host->address()->asString();
        endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_address(
            address.substr(0, address.find_last_of(":")));
        endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address()->set_port_value(
            stoul(address.substr(address.find_last_of(":") + 1, address.length() - 1)));
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
    envoy::api::v2::Cluster cluster_config_;
    clusters_config_.push_back(cluster_config_);
    cluster_config_.set_name("anna" + std::to_string(i));
    cluster_config_.mutable_connect_timeout()->set_seconds(2);
    cluster_config_.mutable_http2_protocol_options();
    cluster_config_.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

    for (int j = 0; j < message->mutable_health_check(i)->endpoints_size(); j++) {
      for (int k = 0; k < message->mutable_health_check(i)->mutable_endpoints(j)->endpoints_size();
           k++) {
        // Add endpoint to cluster
        cluster_config_.add_hosts()->MergeFrom(*message->mutable_health_check(i)
                                                    ->mutable_endpoints(j)
                                                    ->mutable_endpoints(k)
                                                    ->mutable_address());
      }
    }

    for (int l = 0; l < message->mutable_health_check(i)->health_checks_size(); l++) {
      // Add health checker to cluster
      cluster_config_.add_health_checks()->MergeFrom(
          *message->mutable_health_check(i)->mutable_health_checks(l));
    }

    ENVOY_LOG(debug, "New HdsCluster config {} ", cluster_config_.DebugString());

    // Creating HdsCluster
    cluster_.reset(new HdsCluster(runtime_, cluster_config_, bind_config, store_stats,
                                  ssl_context_manager_, secret_manager_, false));

    hds_clusters_.push_back(cluster_);
    std::vector<Upstream::HealthCheckerSharedPtr> cluster_health_checkers;
    for (int l = 0; l < message->mutable_health_check(i)->health_checks_size(); l++) {
      // Creating HealthCheckers
      cluster_health_checkers.push_back(Upstream::HealthCheckerFactory::create(
          *cluster_config_.mutable_health_checks(l), *hds_clusters_[i], runtime_, random_,
          dispatcher_));
    }
    health_checkers_ptr.push_back(cluster_health_checkers);
  }
}
void HdsDelegate::onReceiveMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {
  stats_.requests_.inc();
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());

  // Process the HealthCheckSpecifier message
  processMessage(std::move(message));

  // Initializing Clusters & HealthCheckers
  for (uint i = 0; i < hds_clusters_.size(); i++) {
    for (uint l = 0; l < health_checkers_ptr[i].size(); l++) {
      hds_clusters_[i]->setHealthChecker(health_checkers_ptr[i][l]);
    }
    hds_clusters_[i]->initialize([] {});
    SERVER_RESPONSE_S = message->mutable_interval()->seconds();
    setServerResponseTimer();
  }
}

void HdsDelegate::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
  response_timer_->disableTimer();
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
