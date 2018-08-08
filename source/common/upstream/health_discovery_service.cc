#include "common/upstream/health_discovery_service.h"

#include "envoy/stats/scope.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

HdsDelegate::HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
                         Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
                         Runtime::Loader& runtime, Envoy::Stats::Store& stats,
                         Ssl::ContextManager& ssl_context_manager, Runtime::RandomGenerator& random,
                         ClusterInfoFactory& info_factory,
                         AccessLog::AccessLogManager& access_log_manager, ClusterManager& cm,
                         const LocalInfo::LocalInfo& local_info)
    : stats_{ALL_HDS_STATS(POOL_COUNTER_PREFIX(scope, "hds_delegate."))},
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck")),
      async_client_(std::move(async_client)), dispatcher_(dispatcher), runtime_(runtime),
      store_stats(stats), ssl_context_manager_(ssl_context_manager), random_(random),
      info_factory_(info_factory), access_log_manager_(access_log_manager), cm_(cm),
      local_info_(local_info) {
  health_check_request_.mutable_node()->MergeFrom(node);
  hds_retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  hds_stream_response_timer_ = dispatcher.createTimer([this]() -> void { sendResponse(); });
  establishNewStream();
}

void HdsDelegate::setHdsRetryTimer() {
  hds_retry_timer_->enableTimer(std::chrono::milliseconds(RetryDelayMilliseconds));
}

void HdsDelegate::setHdsStreamResponseTimer() {
  hds_stream_response_timer_->enableTimer(std::chrono::milliseconds(server_response_ms_));
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
  ENVOY_LOG(warn, "HdsDelegate stream/connection failure, will retry in {} ms.",
            RetryDelayMilliseconds);
  stats_.errors_.inc();
  setHdsRetryTimer();
}

// TODO(lilika): Add support for the same endpoint in different clusters/ports
envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse
HdsDelegate::sendResponse() {
  envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse response;
  for (const auto& cluster : hds_clusters_) {
    for (const auto& hosts : cluster->prioritySet().hostSetsPerPriority()) {
      for (const auto& host : hosts->hosts()) {
        auto* endpoint = response.mutable_endpoint_health_response()->add_endpoints_health();
        Network::Utility::addressToProtobufAddress(
            *host->address(), *endpoint->mutable_endpoint()->mutable_address());
        // TODO(lilika): Add support for more granular options of envoy::api::v2::core::HealthStatus
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
  setHdsStreamResponseTimer();
  return response;
}

void HdsDelegate::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::processMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());
  ASSERT(message);

  for (const auto& cluster_health_check : message->health_check()) {
    // Create HdsCluster config
    static const envoy::api::v2::core::BindConfig bind_config;
    envoy::api::v2::Cluster cluster_config;

    cluster_config.set_name(cluster_health_check.cluster_name());
    cluster_config.mutable_connect_timeout()->set_seconds(ClusterTimeoutSeconds);
    cluster_config.mutable_per_connection_buffer_limit_bytes()->set_value(
        ClusterConnectionBufferLimitBytes);

    // Add endpoints to cluster
    for (const auto& locality_endpoints : cluster_health_check.endpoints()) {
      for (const auto& endpoint : locality_endpoints.endpoints()) {
        cluster_config.add_hosts()->MergeFrom(endpoint.address());
      }
    }

    // TODO(lilika): Add support for optional per-endpoint health checks

    // Add healthchecks to cluster
    for (auto& health_check : cluster_health_check.health_checks()) {
      cluster_config.add_health_checks()->MergeFrom(health_check);
    }

    ENVOY_LOG(debug, "New HdsCluster config {} ", cluster_config.DebugString());

    // Create HdsCluster
    hds_clusters_.emplace_back(new HdsCluster(runtime_, cluster_config, bind_config, store_stats,
                                              ssl_context_manager_, false, info_factory_, cm_,
                                              local_info_, dispatcher_, random_));

    hds_clusters_.back()->startHealthchecks(access_log_manager_, runtime_, random_, dispatcher_);
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

  // Set response
  server_response_ms_ = PROTOBUF_GET_MS_REQUIRED(*message, interval);
  setHdsStreamResponseTimer();
}

void HdsDelegate::onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "gRPC config stream closed: {}, {}", status, message);
  hds_stream_response_timer_->disableTimer();
  stream_ = nullptr;
  handleFailure();
}

HdsCluster::HdsCluster(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                       const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager, bool added_via_api,
                       ClusterInfoFactory& info_factory, ClusterManager& cm,
                       const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                       Runtime::RandomGenerator& random)
    : runtime_(runtime), cluster_(cluster), bind_config_(bind_config), stats_(stats),
      ssl_context_manager_(ssl_context_manager), added_via_api_(added_via_api),
      initial_hosts_(new HostVector()) {
  ENVOY_LOG(debug, "Creating an HdsCluster");
  priority_set_.getOrCreateHostSet(0);

  info_ =
      info_factory.createClusterInfo(runtime_, cluster_, bind_config_, stats_, ssl_context_manager_,
                                     added_via_api_, cm, local_info, dispatcher, random);

  for (const auto& host : cluster.hosts()) {
    initial_hosts_->emplace_back(
        new HostImpl(info_, "", Network::Address::resolveProtoAddress(host),
                     envoy::api::v2::core::Metadata::default_instance(), 1,
                     envoy::api::v2::core::Locality().default_instance(),
                     envoy::api::v2::endpoint::Endpoint::HealthCheckConfig().default_instance()));
  }
  initialize([] {});
}

ClusterSharedPtr HdsCluster::create() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

HostVectorConstSharedPtr HdsCluster::createHealthyHostList(const HostVector& hosts) {
  HostVectorSharedPtr healthy_list(new HostVector());
  for (const auto& host : hosts) {
    if (host->healthy()) {
      healthy_list->emplace_back(host);
    }
  }
  return healthy_list;
}

ClusterInfoConstSharedPtr ProdClusterInfoFactory::createClusterInfo(
    Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
    const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
    Ssl::ContextManager& ssl_context_manager, bool added_via_api, ClusterManager& cm,
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Runtime::RandomGenerator& random) {

  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format("cluster.{}.", cluster.name()));

  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);

  Network::TransportSocketFactoryPtr socket_factory =
      Upstream::createTransportSocketFactory(cluster, factory_context);

  return std::make_unique<ClusterInfoImpl>(cluster, bind_config, runtime, std::move(socket_factory),
                                           std::move(scope), added_via_api);
}

void HdsCluster::startHealthchecks(AccessLog::AccessLogManager& access_log_manager,
                                   Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                   Event::Dispatcher& dispatcher) {

  for (auto& health_check : cluster_.health_checks()) {
    health_checkers_.push_back(Upstream::HealthCheckerFactory::create(
        health_check, *this, runtime, random, dispatcher, access_log_manager));
    health_checkers_.back()->start();
  }
}

void HdsCluster::initialize(std::function<void()> callback) {
  initialization_complete_callback_ = callback;
  for (const auto& host : *initial_hosts_) {
    host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  auto healthy = createHealthyHostList(*initial_hosts_);

  first_host_set.updateHosts(initial_hosts_, healthy, HostsPerLocalityImpl::empty(),
                             HostsPerLocalityImpl::empty(), {}, *initial_hosts_, {});
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

} // namespace Upstream
} // namespace Envoy
