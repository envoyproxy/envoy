#include "common/upstream/health_discovery_service.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/service/health/v3/hds.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/config/version_converter.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * TODO(lilika): Add API knob for RetryInitialDelayMilliseconds
 * and RetryMaxDelayMilliseconds, instead of hardcoding them.
 *
 * Parameters of the jittered backoff strategy that defines how often
 * we retry to establish a stream to the management server
 */
static constexpr uint32_t RetryInitialDelayMilliseconds = 1000;
static constexpr uint32_t RetryMaxDelayMilliseconds = 30000;

HdsDelegate::HdsDelegate(Stats::Scope& scope, Grpc::RawAsyncClientPtr async_client,
                         envoy::config::core::v3::ApiVersion transport_api_version,
                         Event::Dispatcher& dispatcher, Runtime::Loader& runtime,
                         Envoy::Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                         ClusterInfoFactory& info_factory,
                         AccessLog::AccessLogManager& access_log_manager, ClusterManager& cm,
                         const LocalInfo::LocalInfo& local_info, Server::Admin& admin,
                         Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                         ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : stats_{ALL_HDS_STATS(POOL_COUNTER_PREFIX(scope, "hds_delegate."))},
      service_method_(Grpc::VersionedMethods(
                          "envoy.service.health.v3.HealthDiscoveryService.StreamHealthCheck",
                          "envoy.service.discovery.v2.HealthDiscoveryService.StreamHealthCheck")
                          .getMethodDescriptorForVersion(transport_api_version)),
      async_client_(std::move(async_client)), transport_api_version_(transport_api_version),
      dispatcher_(dispatcher), runtime_(runtime), store_stats_(stats),
      ssl_context_manager_(ssl_context_manager), info_factory_(info_factory),
      access_log_manager_(access_log_manager), cm_(cm), local_info_(local_info), admin_(admin),
      singleton_manager_(singleton_manager), tls_(tls), validation_visitor_(validation_visitor),
      api_(api) {
  health_check_request_.mutable_health_check_request()->mutable_node()->MergeFrom(
      local_info_.node());
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMilliseconds, RetryMaxDelayMilliseconds, api_.randomGenerator());
  hds_retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  hds_stream_response_timer_ = dispatcher.createTimer([this]() -> void { sendResponse(); });

  // TODO(lilika): Add support for other types of healthchecks
  health_check_request_.mutable_health_check_request()
      ->mutable_capability()
      ->add_health_check_protocols(envoy::service::health::v3::Capability::HTTP);
  health_check_request_.mutable_health_check_request()
      ->mutable_capability()
      ->add_health_check_protocols(envoy::service::health::v3::Capability::TCP);

  establishNewStream();
}

void HdsDelegate::setHdsRetryTimer() {
  const auto retry_ms = std::chrono::milliseconds(backoff_strategy_->nextBackOffMs());
  ENVOY_LOG(warn, "HdsDelegate stream/connection failure, will retry in {} ms.", retry_ms.count());

  hds_retry_timer_->enableTimer(retry_ms);
}

void HdsDelegate::setHdsStreamResponseTimer() {
  hds_stream_response_timer_->enableTimer(std::chrono::milliseconds(server_response_ms_));
}

void HdsDelegate::establishNewStream() {
  ENVOY_LOG(debug, "Establishing new gRPC bidi stream for {}", service_method_.DebugString());
  stream_ = async_client_->start(service_method_, *this, Http::AsyncClient::StreamOptions());
  if (stream_ == nullptr) {
    ENVOY_LOG(warn, "Unable to establish new stream");
    handleFailure();
    return;
  }

  Config::VersionConverter::prepareMessageForGrpcWire(health_check_request_,
                                                      transport_api_version_);
  ENVOY_LOG(debug, "Sending HealthCheckRequest {} ", health_check_request_.DebugString());
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
  backoff_strategy_->reset();
}

void HdsDelegate::handleFailure() {
  stats_.errors_.inc();
  setHdsRetryTimer();
}

// TODO(lilika): Add support for the same endpoint in different clusters/ports
envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse HdsDelegate::sendResponse() {
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse response;

  for (const auto& cluster : hds_clusters_) {
    // Add cluster health response and set name.
    auto* cluster_health =
        response.mutable_endpoint_health_response()->add_cluster_endpoints_health();
    cluster_health->set_cluster_name(cluster->info()->name());

    // Iterate through all hosts in our priority set.
    for (const auto& hosts : cluster->prioritySet().hostSetsPerPriority()) {
      // Get a grouping of hosts by locality.
      for (const auto& locality_hosts : hosts->hostsPerLocality().get()) {
        // For this locality, add the response grouping.
        envoy::service::health::v3::LocalityEndpointsHealth* locality_health =
            cluster_health->add_locality_endpoints_health();
        locality_health->mutable_locality()->MergeFrom(locality_hosts[0]->locality());

        // Add all hosts to this locality.
        for (const auto& host : locality_hosts) {
          // Add this endpoint's health status to this locality grouping.
          auto* endpoint = locality_health->add_endpoints_health();
          Network::Utility::addressToProtobufAddress(
              *host->address(), *endpoint->mutable_endpoint()->mutable_address());
          // TODO(lilika): Add support for more granular options of
          // envoy::config::core::v3::HealthStatus
          if (host->health() == Host::Health::Healthy) {
            endpoint->set_health_status(envoy::config::core::v3::HEALTHY);
          } else {
            switch (host->getActiveHealthFailureType()) {
            case Host::ActiveHealthFailureType::TIMEOUT:
              endpoint->set_health_status(envoy::config::core::v3::TIMEOUT);
              break;
            case Host::ActiveHealthFailureType::UNHEALTHY:
            case Host::ActiveHealthFailureType::UNKNOWN:
              endpoint->set_health_status(envoy::config::core::v3::UNHEALTHY);
              break;
            default:
              NOT_REACHED_GCOVR_EXCL_LINE;
              break;
            }
          }

          // TODO(drewsortega): remove this once we are on v4 and endpoint_health_response is
          // removed. Copy this endpoint's health info to the legacy flat-list.
          response.mutable_endpoint_health_response()->add_endpoints_health()->MergeFrom(*endpoint);
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

void HdsDelegate::onCreateInitialMetadata(Http::RequestHeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::processMessage(
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());
  ASSERT(message);

  for (const auto& cluster_health_check : message->cluster_health_checks()) {
    // Create HdsCluster config
    static const envoy::config::core::v3::BindConfig bind_config;
    envoy::config::cluster::v3::Cluster cluster_config;

    cluster_config.set_name(cluster_health_check.cluster_name());
    cluster_config.mutable_connect_timeout()->set_seconds(ClusterTimeoutSeconds);
    cluster_config.mutable_per_connection_buffer_limit_bytes()->set_value(
        ClusterConnectionBufferLimitBytes);

    // Add endpoints to cluster
    for (const auto& locality_endpoints : cluster_health_check.locality_endpoints()) {
      // add endpoint group by locality to config
      auto* endpoints = cluster_config.mutable_load_assignment()->add_endpoints();
      // if this group contains locality information, save it.
      if (locality_endpoints.has_locality()) {
        endpoints->mutable_locality()->MergeFrom(locality_endpoints.locality());
      }

      // add all endpoints for this locality group to the config
      for (const auto& endpoint : locality_endpoints.endpoints()) {
        endpoints->add_lb_endpoints()->mutable_endpoint()->mutable_address()->MergeFrom(
            endpoint.address());
      }
    }

    // TODO(lilika): Add support for optional per-endpoint health checks

    // Add healthchecks to cluster
    for (auto& health_check : cluster_health_check.health_checks()) {
      cluster_config.add_health_checks()->MergeFrom(health_check);
    }

    // Add transport_socket_match to cluster for use in host connections.
    cluster_config.mutable_transport_socket_matches()->MergeFrom(
        cluster_health_check.transport_socket_matches());

    ENVOY_LOG(debug, "New HdsCluster config {} ", cluster_config.DebugString());

    // Create HdsCluster
    hds_clusters_.emplace_back(new HdsCluster(admin_, runtime_, std::move(cluster_config),
                                              bind_config, store_stats_, ssl_context_manager_,
                                              false, info_factory_, cm_, local_info_, dispatcher_,
                                              singleton_manager_, tls_, validation_visitor_, api_));
    hds_clusters_.back()->initialize([] {});

    hds_clusters_.back()->startHealthchecks(access_log_manager_, runtime_, dispatcher_, api_);
  }
}

// TODO(lilika): Add support for subsequent HealthCheckSpecifier messages that
// might modify the HdsClusters
void HdsDelegate::onReceiveMessage(
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
  stats_.requests_.inc();
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());

  // Validate message fields
  try {
    MessageUtil::validate(*message, validation_visitor_);
  } catch (const ProtoValidationException& ex) {
    // Increment error count
    stats_.errors_.inc();
    ENVOY_LOG(warn, "Unable to validate health check specifier: {}", ex.what());

    // Do not continue processing message
    return;
  }

  // Reset
  hds_clusters_.clear();

  // Set response
  auto server_response_ms = PROTOBUF_GET_MS_OR_DEFAULT(*message, interval, 1000);

  // Process the HealthCheckSpecifier message.
  processMessage(std::move(message));

  if (server_response_ms_ != server_response_ms) {
    server_response_ms_ = server_response_ms;
    setHdsStreamResponseTimer();
  }
}

void HdsDelegate::onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) {
  ENVOY_LOG(warn, "{} gRPC config stream closed: {}, {}", service_method_.name(), status, message);
  hds_stream_response_timer_->disableTimer();
  stream_ = nullptr;
  server_response_ms_ = 0;
  handleFailure();
}

HdsCluster::HdsCluster(Server::Admin& admin, Runtime::Loader& runtime,
                       envoy::config::cluster::v3::Cluster cluster,
                       const envoy::config::core::v3::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager, bool added_via_api,
                       ClusterInfoFactory& info_factory, ClusterManager& cm,
                       const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                       Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                       ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : runtime_(runtime), cluster_(std::move(cluster)), bind_config_(bind_config), stats_(stats),
      ssl_context_manager_(ssl_context_manager), added_via_api_(added_via_api),
      initial_hosts_(new HostVector()), validation_visitor_(validation_visitor) {
  ENVOY_LOG(debug, "Creating an HdsCluster");
  priority_set_.getOrCreateHostSet(0);

  info_ = info_factory.createClusterInfo(
      {admin, runtime_, cluster_, bind_config_, stats_, ssl_context_manager_, added_via_api_, cm,
       local_info, dispatcher, singleton_manager, tls, validation_visitor, api});

  // Temporary structure to hold Host pointers grouped by locality, to build
  // initial_hosts_per_locality_.
  std::vector<HostVector> hosts_by_locality;
  hosts_by_locality.reserve(cluster_.load_assignment().endpoints_size());

  // Iterate over every endpoint in every cluster.
  for (const auto& locality_endpoints : cluster_.load_assignment().endpoints()) {
    // Add a locality grouping to the hosts sorted by locality.
    hosts_by_locality.emplace_back();
    hosts_by_locality.back().reserve(locality_endpoints.lb_endpoints_size());

    for (const auto& host : locality_endpoints.lb_endpoints()) {
      // Initialize an endpoint host object.
      HostSharedPtr endpoint = std::make_shared<HostImpl>(
          info_, "", Network::Address::resolveProtoAddress(host.endpoint().address()), nullptr, 1,
          locality_endpoints.locality(),
          envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
          envoy::config::core::v3::UNKNOWN);
      // Add this host/endpoint pointer to our flat list of endpoints for health checking.
      initial_hosts_->push_back(endpoint);
      // Add this host/endpoint pointer to our structured list by locality so results can be
      // requested by locality.
      hosts_by_locality.back().push_back(endpoint);
    }
  }
  // Create the HostsPerLocality.
  initial_hosts_per_locality_ =
      std::make_shared<Envoy::Upstream::HostsPerLocalityImpl>(std::move(hosts_by_locality), false);
}

ClusterSharedPtr HdsCluster::create() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

ClusterInfoConstSharedPtr
ProdClusterInfoFactory::createClusterInfo(const CreateClusterInfoParams& params) {
  Envoy::Stats::ScopePtr scope =
      params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));

  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      params.admin_, params.ssl_context_manager_, *scope, params.cm_, params.local_info_,
      params.dispatcher_, params.stats_, params.singleton_manager_, params.tls_,
      params.validation_visitor_, params.api_);

  // TODO(JimmyCYJ): Support SDS for HDS cluster.
  Network::TransportSocketFactoryPtr socket_factory =
      Upstream::createTransportSocketFactory(params.cluster_, factory_context);
  auto socket_matcher = std::make_unique<TransportSocketMatcherImpl>(
      params.cluster_.transport_socket_matches(), factory_context, socket_factory, *scope);

  return std::make_unique<ClusterInfoImpl>(params.cluster_, params.bind_config_, params.runtime_,
                                           std::move(socket_matcher), std::move(scope),
                                           params.added_via_api_, factory_context);
}

void HdsCluster::startHealthchecks(AccessLog::AccessLogManager& access_log_manager,
                                   Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
                                   Api::Api& api) {
  for (auto& health_check : cluster_.health_checks()) {
    health_checkers_.push_back(Upstream::HealthCheckerFactory::create(
        health_check, *this, runtime, dispatcher, access_log_manager, validation_visitor_, api));
    health_checkers_.back()->start();
  }
}

void HdsCluster::initialize(std::function<void()> callback) {
  initialization_complete_callback_ = callback;
  for (const auto& host : *initial_hosts_) {
    host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  // Use the ungrouped and grouped hosts lists to retain locality structure in the priority set.
  priority_set_.updateHosts(
      0, HostSetImpl::partitionHosts(initial_hosts_, initial_hosts_per_locality_), {},
      *initial_hosts_, {}, absl::nullopt);
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

} // namespace Upstream
} // namespace Envoy
