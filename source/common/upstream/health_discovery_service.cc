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
                         Random::RandomGenerator& random, ClusterInfoFactory& info_factory,
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
      ssl_context_manager_(ssl_context_manager), random_(random), info_factory_(info_factory),
      access_log_manager_(access_log_manager), cm_(cm), local_info_(local_info), admin_(admin),
      singleton_manager_(singleton_manager), tls_(tls), specifier_hash_(0),
      hds_clusters_hash_map_(
          std::make_unique<absl::flat_hash_map<uint64_t, Envoy::Upstream::HdsClusterPtr>>()),
      hds_clusters_name_map_(
          std::make_unique<absl::flat_hash_map<std::string, Envoy::Upstream::HdsClusterPtr>>()),
      validation_visitor_(validation_visitor), api_(api) {
  health_check_request_.mutable_health_check_request()->mutable_node()->MergeFrom(
      local_info_.node());
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMilliseconds, RetryMaxDelayMilliseconds, random_);
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

envoy::config::cluster::v3::Cluster HdsDelegate::createClusterConfig(
    const envoy::service::health::v3::ClusterHealthCheck& cluster_health_check) {
  // Create HdsCluster config
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

  ENVOY_LOG(debug, "New HdsCluster config {} ", cluster_config.DebugString());

  return cluster_config;
}

HdsClusterPtr
HdsDelegate::tryUpdateHdsCluster(HdsClusterPtr cluster,
                                 const envoy::config::cluster::v3::Cluster& cluster_config) {
  cluster->update(admin_, cluster_config, info_factory_, cm_, local_info_, dispatcher_, random_,
                  singleton_manager_, tls_, validation_visitor_, api_, access_log_manager_,
                  runtime_);
  return cluster;
}

HdsClusterPtr
HdsDelegate::createHdsCluster(const envoy::config::cluster::v3::Cluster& cluster_config) {
  static const envoy::config::core::v3::BindConfig bind_config;

  // Create HdsCluster.
  auto new_cluster = std::make_shared<HdsCluster>(
      admin_, runtime_, std::move(cluster_config), bind_config, store_stats_, ssl_context_manager_,
      false, info_factory_, cm_, local_info_, dispatcher_, random_, singleton_manager_, tls_,
      validation_visitor_, api_);

  // Begin HCs in the background.
  new_cluster->initialize([] {});
  new_cluster->initHealthchecks(access_log_manager_, runtime_, random_, dispatcher_, api_);

  return new_cluster;
}

void HdsDelegate::processMessage(
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());
  ASSERT(message);

  // Maps to replace the current member variable versions.
  std::unique_ptr<absl::flat_hash_map<uint64_t, HdsClusterPtr>> new_hds_clusters_hash_map =
      std::make_unique<absl::flat_hash_map<uint64_t, HdsClusterPtr>>();
  std::unique_ptr<absl::flat_hash_map<std::string, HdsClusterPtr>> new_hds_clusters_name_map =
      std::make_unique<absl::flat_hash_map<std::string, HdsClusterPtr>>();

  for (const auto& cluster_health_check : message->cluster_health_checks()) {
    const uint64_t cluster_config_hash = MessageUtil::hash(cluster_health_check);
    HdsClusterPtr cluster_ptr;

    // If this cluster with the exact configuration is already being tracked, skip it.
    auto cluster_map_pair = hds_clusters_hash_map_->find(cluster_config_hash);
    if (cluster_map_pair != hds_clusters_hash_map_->end()) {
      // This cluster with the exact same configuration already exists, so just reuse it.
      ENVOY_LOG(debug, "HDS Cluster already exists with this configuration, skipping.");
      cluster_ptr = cluster_map_pair->second;
    } else {
      // Create a new configuration for a cluster based on our different or new config.
      auto cluster_config = createClusterConfig(cluster_health_check);

      // If this particular cluster configuration happens to have a name, then it is possible
      // this particular cluster exists in the name map. We check and if we found a match,
      // attempt to update this cluster. If no match was found, either the cluster name is empty
      // or we have not seen a cluster by this name before. In either case, create a new cluster.
      auto cluster_map_pair = hds_clusters_name_map_->find(cluster_health_check.cluster_name());
      if (cluster_map_pair != hds_clusters_name_map_->end()) {
        // We have a previous cluster with this name, update.
        cluster_ptr = tryUpdateHdsCluster(cluster_map_pair->second, cluster_config);
      } else {
        // There is no cluster with this name previously or its an empty string, so just create a
        // new cluster.
        cluster_ptr = createHdsCluster(cluster_config);
      }
    }

    // If this had a non-empty name, add this cluster to the name map so it can be updated in the
    // future.
    if (!cluster_health_check.cluster_name().empty()) {
      // Since this cluster has a name, add it to our by-name map.
      hds_clusters_name_map_->insert({cluster_health_check.cluster_name(), cluster_ptr});
    }

    // Add to our remaining data structures.
    hds_clusters_.push_back(cluster_ptr);
    new_hds_clusters_hash_map->insert({cluster_config_hash, cluster_ptr});
  }

  // Overwrite our map data structures.
  hds_clusters_hash_map_ = std::move(new_hds_clusters_hash_map);
  hds_clusters_name_map_ = std::move(new_hds_clusters_name_map);
}

void HdsDelegate::onReceiveMessage(
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
  stats_.requests_.inc();
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());

  const uint64_t hash = MessageUtil::hash(*message);

  if (hash == specifier_hash_) {
    ENVOY_LOG(debug, "New health check specifier is unchanged, no action taken.");
    return;
  }

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

  // Update the stored hash.
  specifier_hash_ = hash;

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
                       Random::RandomGenerator& random, Singleton::Manager& singleton_manager,
                       ThreadLocal::SlotAllocator& tls,
                       ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : runtime_(runtime), cluster_(std::move(cluster)), bind_config_(bind_config), stats_(stats),
      ssl_context_manager_(ssl_context_manager), added_via_api_(added_via_api),
      hosts_(new HostVector()),
      hosts_map_(std::make_unique<absl::flat_hash_map<uint64_t, HostSharedPtr>>()),
      health_checkers_map_(
          std::make_unique<absl::flat_hash_map<uint64_t, Upstream::HealthCheckerSharedPtr>>()),
      validation_visitor_(validation_visitor) {
  ENVOY_LOG(debug, "Creating an HdsCluster");
  priority_set_.getOrCreateHostSet(0);
  config_hash_ = MessageUtil::hash(cluster_);
  endpoints_hash_ = RepeatedPtrUtil::hash(cluster_.load_assignment().endpoints());
  health_checkers_hash_ = RepeatedPtrUtil::hash(cluster_.health_checks());

  info_ = info_factory.createClusterInfo(
      {admin, runtime_, cluster_, bind_config_, stats_, ssl_context_manager_, added_via_api_, cm,
       local_info, dispatcher, random, singleton_manager, tls, validation_visitor, api});

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
      const auto endpoint_hash = MessageUtil::hash(host);
      // Initialize an endpoint host object.
      HostSharedPtr endpoint = std::make_shared<HostImpl>(
          info_, "", Network::Address::resolveProtoAddress(host.endpoint().address()), nullptr, 1,
          locality_endpoints.locality(),
          envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
          envoy::config::core::v3::UNKNOWN);
      // Add this host/endpoint pointer to our flat list of endpoints for health checking.
      hosts_->push_back(endpoint);
      // Add this host/endpoint pointer to our structured list by locality so results can be
      // requested by locality.
      hosts_by_locality.back().push_back(endpoint);
      // Add this host/endpoint pointer to our map so we can rebuild this later.
      hosts_map_->insert({endpoint_hash, endpoint});
    }
  }
  // Create the HostsPerLocality.
  hosts_per_locality_ =
      std::make_shared<Envoy::Upstream::HostsPerLocalityImpl>(std::move(hosts_by_locality), false);
}

void HdsCluster::update(Server::Admin& admin, envoy::config::cluster::v3::Cluster cluster,
                        ClusterInfoFactory& info_factory, ClusterManager& cm,
                        const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
                        Random::RandomGenerator& random, Singleton::Manager& singleton_manager,
                        ThreadLocal::SlotAllocator& tls,
                        ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                        AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime) {
  const uint64_t config_hash = MessageUtil::hash(cluster);
  // if this is a different config then what we already have, update the cluster.
  if (config_hash_ != config_hash) {
    config_hash_ = config_hash;
    cluster_ = std::move(cluster);

    // always update our info_
    info_ = info_factory.createClusterInfo(
        {admin, runtime_, cluster_, bind_config_, stats_, ssl_context_manager_, added_via_api_, cm,
         local_info, dispatcher, random, singleton_manager, tls, validation_visitor, api});

    const auto& endpoints = cluster.load_assignment().endpoints();
    const uint64_t endpoints_hash = RepeatedPtrUtil::hash(endpoints);
    if (endpoints_hash_ != endpoints_hash) {
      endpoints_hash_ = endpoints_hash;
      updateHosts(endpoints);
    }

    const uint64_t health_checkers_hash = RepeatedPtrUtil::hash(cluster.health_checks());
    if (health_checkers_hash_ != health_checkers_hash) {
      health_checkers_hash_ = health_checkers_hash;
      updateHealthchecks(cluster_.health_checks(), access_log_manager, runtime, random, dispatcher,
                         api);
    }
  }
}

void HdsCluster::updateHealthchecks(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks,
    AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime,
    Random::RandomGenerator& random, Event::Dispatcher& dispatcher, Api::Api& api) {
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers;
  std::unique_ptr<absl::flat_hash_map<uint64_t, Upstream::HealthCheckerSharedPtr>>
      health_checkers_map =
          std::make_unique<absl::flat_hash_map<uint64_t, Upstream::HealthCheckerSharedPtr>>();

  for (auto& health_check : health_checks) {
    const uint64_t hash = MessageUtil::hash(health_check);

    // Check to see if this exact same health_check config already has a health checker.
    auto health_checker = health_checkers_map_->find(hash);
    if (health_checker != health_checkers_map_->end()) {
      // If it does, use it.
      health_checkers_map->insert({hash, health_checker->second});
      health_checkers.push_back(health_checker->second);
    } else {
      // If it does not, create a new one.
      auto new_health_checker =
          Upstream::HealthCheckerFactory::create(health_check, *this, runtime, random, dispatcher,
                                                 access_log_manager, validation_visitor_, api);
      health_checkers_map->insert({hash, new_health_checker});
      health_checkers.push_back(new_health_checker);
    }
  }

  // replace our member data structures with our newly created ones.
  health_checkers_ = std::move(health_checkers);
  health_checkers_map_ = std::move(health_checkers_map);
}
void HdsCluster::updateHosts(
    const Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::LocalityLbEndpoints>&
        locality_endpoints) {
  // TODO(drewsortega)
  UNREFERENCED_PARAMETER(locality_endpoints);
  //  create hosts - vector of HostSharedPtr
  //  create hosts_added - vector of HostSharedPtr
  //  create hosts_removed - vector of HostSharedPtr
  //  create hosts_by_locality - vector of (vector of HostSharedPtr)
  //  create hosts_map - map of HostSharedPtr with hash of config as key
  //
  //  endpoints : locality_endpoints:
  //    - add new vector of hosts to hosts_by_locality
  //    endpoint : endpoints:
  //      create empty variable 'host'
  //      if endpoint not in host_map_:
  //        - host = new Host
  //        - add host to hosts_added
  //      else if endpoint in hosts_map_:
  //        - host = hosts_map[hash]
  //
  //      - add host to this locality in hosts_by_locality
  //      - add host to hosts
  //      - add host to hosts_map
  //
  //  host : hosts_map_
  //    if host not in hosts_map:
  //      add host to hosts_removed
  //
  //  replace hosts_ with new hosts
  //  replace hosts_map_ with hosts_map
  //  create new hosts_per_locality_ with hosts_by_locality
  //  update priority_set_ with hosts_added, hosts_removed_, hosts_per_locality_
}

ClusterSharedPtr HdsCluster::create() { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }

ClusterInfoConstSharedPtr
ProdClusterInfoFactory::createClusterInfo(const CreateClusterInfoParams& params) {
  Envoy::Stats::ScopePtr scope =
      params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));

  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      params.admin_, params.ssl_context_manager_, *scope, params.cm_, params.local_info_,
      params.dispatcher_, params.random_, params.stats_, params.singleton_manager_, params.tls_,
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

void HdsCluster::initHealthchecks(AccessLog::AccessLogManager& access_log_manager,
                                  Runtime::Loader& runtime, Random::RandomGenerator& random,
                                  Event::Dispatcher& dispatcher, Api::Api& api) {
  for (auto& health_check : cluster_.health_checks()) {
    health_checkers_.push_back(
        Upstream::HealthCheckerFactory::create(health_check, *this, runtime, random, dispatcher,
                                               access_log_manager, validation_visitor_, api));
    health_checkers_.back()->start();
  }
}

void HdsCluster::initialize(std::function<void()> callback) {
  initialization_complete_callback_ = callback;

  // If this function gets called again we do not want to touch the priority set again with the
  // initial hosts, because the hosts may have changed.
  if (!initialized_) {
    for (const auto& host : *hosts_) {
      host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
    // Use the ungrouped and grouped hosts lists to retain locality structure in the priority set.
    priority_set_.updateHosts(0, HostSetImpl::partitionHosts(hosts_, hosts_per_locality_), {},
                              *hosts_, {}, absl::nullopt);

    initialized_ = true;
  }
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr&) {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

} // namespace Upstream
} // namespace Envoy
