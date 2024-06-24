#include "source/common/upstream/health_discovery_service.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/service/health/v3/hds.pb.validate.h"
#include "envoy/stats/scope.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/upstream_impl.h"

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

HdsDelegate::HdsDelegate(Server::Configuration::ServerFactoryContext& server_context,
                         Stats::Scope& scope, Grpc::RawAsyncClientPtr async_client,
                         Envoy::Stats::Store& stats, Ssl::ContextManager& ssl_context_manager,
                         ClusterInfoFactory& info_factory)
    : stats_{ALL_HDS_STATS(POOL_COUNTER_PREFIX(scope, "hds_delegate."))},
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.health.v3.HealthDiscoveryService.StreamHealthCheck")),
      async_client_(std::move(async_client)), dispatcher_(server_context.mainThreadDispatcher()),
      server_context_(server_context), store_stats_(stats),
      ssl_context_manager_(ssl_context_manager), info_factory_(info_factory),
      tls_(server_context_.threadLocal()) {
  health_check_request_.mutable_health_check_request()->mutable_node()->MergeFrom(
      server_context.localInfo().node());
  backoff_strategy_ = std::make_unique<JitteredExponentialBackOffStrategy>(
      RetryInitialDelayMilliseconds, RetryMaxDelayMilliseconds,
      server_context_.api().randomGenerator());
  hds_retry_timer_ = dispatcher_.createTimer([this]() -> void { establishNewStream(); });
  hds_stream_response_timer_ = dispatcher_.createTimer([this]() -> void { sendResponse(); });

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
          if (host->coarseHealth() == Host::Health::Healthy) {
            endpoint->set_health_status(envoy::config::core::v3::HEALTHY);
          } else {
            if (host->healthFlagGet(Host::HealthFlag::ACTIVE_HC_TIMEOUT)) {
              endpoint->set_health_status(envoy::config::core::v3::TIMEOUT);
            } else {
              endpoint->set_health_status(envoy::config::core::v3::UNHEALTHY);
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
      if (endpoint.has_health_check_config() &&
          endpoint.health_check_config().disable_active_health_check()) {
        ENVOY_LOG(debug, "Skip adding the endpoint {} with optional disabled health check for HDS.",
                  endpoint.DebugString());
        continue;
      }
      auto* new_endpoint = endpoints->add_lb_endpoints()->mutable_endpoint();
      new_endpoint->mutable_address()->MergeFrom(endpoint.address());
      new_endpoint->mutable_health_check_config()->MergeFrom(endpoint.health_check_config());
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

  return cluster_config;
}

absl::Status
HdsDelegate::updateHdsCluster(HdsClusterPtr cluster,
                              const envoy::config::cluster::v3::Cluster& cluster_config,
                              const envoy::config::core::v3::BindConfig& bind_config) {
  return cluster->update(cluster_config, bind_config, info_factory_, tls_);
}

HdsClusterPtr
HdsDelegate::createHdsCluster(const envoy::config::cluster::v3::Cluster& cluster_config,
                              const envoy::config::core::v3::BindConfig& bind_config) {
  // Create HdsCluster.
  auto new_cluster =
      std::make_shared<HdsCluster>(server_context_, std::move(cluster_config), bind_config,
                                   store_stats_, ssl_context_manager_, false, info_factory_, tls_);

  // Begin HCs in the background.
  new_cluster->initialize([] {});
  new_cluster->initHealthchecks();

  return new_cluster;
}

absl::Status HdsDelegate::processMessage(
    std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) {
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());
  ASSERT(message);
  std::vector<HdsClusterPtr> hds_clusters;
  // Maps to replace the current member variable versions.
  absl::flat_hash_map<std::string, HdsClusterPtr> new_hds_clusters_name_map;

  for (const auto& cluster_health_check : message->cluster_health_checks()) {
    if (!new_hds_clusters_name_map.contains(cluster_health_check.cluster_name())) {
      HdsClusterPtr cluster_ptr;

      // Create a new configuration for a cluster based on our different or new config.
      auto cluster_config = createClusterConfig(cluster_health_check);

      // If this particular cluster configuration happens to have a name, then it is possible
      // this particular cluster exists in the name map. We check and if we found a match,
      // attempt to update this cluster. If no match was found, either the cluster name is empty
      // or we have not seen a cluster by this name before. In either case, create a new cluster.
      auto cluster_map_pair = hds_clusters_name_map_.find(cluster_health_check.cluster_name());
      if (cluster_map_pair != hds_clusters_name_map_.end()) {
        // We have a previous cluster with this name, update.
        cluster_ptr = cluster_map_pair->second;
        absl::Status status = updateHdsCluster(cluster_ptr, cluster_config,
                                               cluster_health_check.upstream_bind_config());
        if (!status.ok()) {
          return status;
        }
      } else {
        // There is no cluster with this name previously or its an empty string, so just create a
        // new cluster.
        cluster_ptr = createHdsCluster(cluster_config, cluster_health_check.upstream_bind_config());
      }

      // If this cluster does not have a name, do not add it to the name map since cluster_name is
      // an optional field, and reconstruct these clusters on every update.
      if (!cluster_health_check.cluster_name().empty()) {
        // Since this cluster has a name, add it to our by-name map so we can update it later.
        new_hds_clusters_name_map.insert({cluster_health_check.cluster_name(), cluster_ptr});
      } else {
        ENVOY_LOG(warn,
                  "HDS Cluster has no cluster_name, it will be recreated instead of updated on "
                  "every reconfiguration.");
      }

      // Add this cluster to the flat list for health checking.
      hds_clusters.push_back(cluster_ptr);
    } else {
      ENVOY_LOG(warn, "An HDS Cluster with this cluster_name has already been added, not using.");
    }
  }

  // Overwrite our map data structures.
  hds_clusters_name_map_ = std::move(new_hds_clusters_name_map);
  hds_clusters_ = std::move(hds_clusters);

  // TODO: add stats reporting for number of clusters added, removed, and reused.
  return absl::OkStatus();
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
  TRY_ASSERT_MAIN_THREAD {
    MessageUtil::validate(*message,
                          server_context_.messageValidationContext().dynamicValidationVisitor());
  }
  END_TRY
  CATCH(const ProtoValidationException& ex, {
    // Increment error count
    stats_.errors_.inc();
    ENVOY_LOG(warn, "Unable to validate health check specifier: {}", ex.what());

    // Do not continue processing message
    return;
  });

  // Set response
  auto server_response_ms = PROTOBUF_GET_MS_OR_DEFAULT(*message, interval, 1000);

  /// Process the HealthCheckSpecifier message.
  absl::Status status = processMessage(std::move(message));
  if (!status.ok()) {
    stats_.errors_.inc();
    ENVOY_LOG(warn, "Unable to validate health check specifier: {}", status.message());
    // Do not continue processing message
    return;
  }

  stats_.updates_.inc();

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
  specifier_hash_ = 0;
  handleFailure();
}

HdsCluster::HdsCluster(Server::Configuration::ServerFactoryContext& server_context,
                       envoy::config::cluster::v3::Cluster cluster,
                       const envoy::config::core::v3::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager, bool added_via_api,
                       ClusterInfoFactory& info_factory, ThreadLocal::SlotAllocator& tls)
    : server_context_(server_context), cluster_(std::move(cluster)), stats_(stats),
      ssl_context_manager_(ssl_context_manager), added_via_api_(added_via_api),
      hosts_(new HostVector()), time_source_(server_context_.mainThreadDispatcher().timeSource()) {
  ENVOY_LOG(debug, "Creating an HdsCluster");
  priority_set_.getOrCreateHostSet(0);
  // Set initial hashes for possible delta updates.
  config_hash_ = MessageUtil::hash(cluster_);
  socket_match_hash_ = RepeatedPtrUtil::hash(cluster_.transport_socket_matches());

  info_ = info_factory.createClusterInfo(
      {server_context, cluster_, bind_config, stats_, ssl_context_manager_, added_via_api_, tls});

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
      const LocalityEndpointTuple endpoint_key = {locality_endpoints.locality(), host};
      // Initialize an endpoint host object.
      auto address_or_error = Network::Address::resolveProtoAddress(host.endpoint().address());
      THROW_IF_STATUS_NOT_OK(address_or_error, throw);
      HostSharedPtr endpoint = std::make_shared<HostImpl>(
          info_, "", std::move(address_or_error.value()), nullptr, 1, locality_endpoints.locality(),
          host.endpoint().health_check_config(), 0, envoy::config::core::v3::UNKNOWN, time_source_);
      // Add this host/endpoint pointer to our flat list of endpoints for health checking.
      hosts_->push_back(endpoint);
      // Add this host/endpoint pointer to our structured list by locality so results can be
      // requested by locality.
      hosts_by_locality.back().push_back(endpoint);
      // Add this host/endpoint pointer to our map so we can rebuild this later.
      hosts_map_.insert({endpoint_key, endpoint});
    }
  }
  // Create the HostsPerLocality.
  hosts_per_locality_ =
      std::make_shared<Envoy::Upstream::HostsPerLocalityImpl>(std::move(hosts_by_locality), false);
}

absl::Status HdsCluster::update(envoy::config::cluster::v3::Cluster cluster,
                                const envoy::config::core::v3::BindConfig& bind_config,
                                ClusterInfoFactory& info_factory, ThreadLocal::SlotAllocator& tls) {

  // check to see if the config changed. If it did, update.
  const uint64_t config_hash = MessageUtil::hash(cluster);
  if (config_hash_ != config_hash) {
    config_hash_ = config_hash;
    cluster_ = std::move(cluster);

    // Check to see if our list of socket matches have changed. If they have, create a new matcher
    // in info_.
    bool update_cluster_info = false;
    const uint64_t socket_match_hash = RepeatedPtrUtil::hash(cluster_.transport_socket_matches());
    if (socket_match_hash_ != socket_match_hash) {
      socket_match_hash_ = socket_match_hash;
      update_cluster_info = true;
      info_ = info_factory.createClusterInfo({server_context_, cluster_, bind_config, stats_,
                                              ssl_context_manager_, added_via_api_, tls});
    }

    // Check to see if anything in the endpoints list has changed.
    updateHosts(cluster_.load_assignment().endpoints(), update_cluster_info);

    // Check to see if any of the health checkers have changed.
    absl::Status status = updateHealthchecks(cluster_.health_checks());
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status HdsCluster::updateHealthchecks(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks) {
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers;
  HealthCheckerMap health_checkers_map;

  for (const auto& health_check : health_checks) {
    // Check to see if this exact same health_check config already has a health checker.
    auto health_checker = health_checkers_map_.find(health_check);
    if (health_checker != health_checkers_map_.end()) {
      // If it does, use it.
      health_checkers_map.insert({health_check, health_checker->second});
      health_checkers.push_back(health_checker->second);
    } else {
      // If it does not, create a new one.
      auto checker_or_error =
          Upstream::HealthCheckerFactory::create(health_check, *this, server_context_);
      RETURN_IF_STATUS_NOT_OK(checker_or_error);
      auto new_health_checker = checker_or_error.value();
      health_checkers_map.insert({health_check, new_health_checker});
      health_checkers.push_back(new_health_checker);

      // Start these health checks now because upstream assumes they already have been started.
      new_health_checker->start();
    }
  }

  // replace our member data structures with our newly created ones.
  health_checkers_ = std::move(health_checkers);
  health_checkers_map_ = std::move(health_checkers_map);

  // TODO: add stats reporting for number of health checkers added, removed, and reused.
  return absl::OkStatus();
}

void HdsCluster::updateHosts(
    const Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::LocalityLbEndpoints>&
        locality_endpoints,
    bool update_cluster_info) {
  // Create the data structures needed for PrioritySet::update.
  HostVectorSharedPtr hosts = std::make_shared<std::vector<HostSharedPtr>>();
  std::vector<HostSharedPtr> hosts_added;
  std::vector<HostSharedPtr> hosts_removed;
  std::vector<HostVector> hosts_by_locality;

  // Use for delta update comparison.
  HostsMap hosts_map;

  for (auto& endpoints : locality_endpoints) {
    hosts_by_locality.emplace_back();
    for (auto& endpoint : endpoints.lb_endpoints()) {
      LocalityEndpointTuple endpoint_key = {endpoints.locality(), endpoint};

      // Check to see if this exact Locality+Endpoint has been seen before.
      // Also, if we made changes to our info, re-create all endpoints.
      auto host_pair = hosts_map_.find(endpoint_key);
      HostSharedPtr host;
      if (!update_cluster_info && host_pair != hosts_map_.end()) {
        // If we have this exact pair, save the shared pointer.
        host = host_pair->second;
      } else {
        // We do not have this endpoint saved, so create a new one.
        auto address_or_error =
            Network::Address::resolveProtoAddress(endpoint.endpoint().address());
        THROW_IF_STATUS_NOT_OK(address_or_error, throw);
        host = std::make_shared<HostImpl>(info_, "", std::move(address_or_error.value()), nullptr,
                                          1, endpoints.locality(),
                                          endpoint.endpoint().health_check_config(), 0,
                                          envoy::config::core::v3::UNKNOWN, time_source_);

        // Set the initial health status as in HdsCluster::initialize.
        host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

        // Add to our hosts added list and save the shared pointer.
        hosts_added.push_back(host);
      }

      // No matter if it is reused or new, always add to these data structures.
      hosts_by_locality.back().push_back(host);
      hosts->push_back(host);
      hosts_map.insert({endpoint_key, host});
    }
  }

  // Compare the old map to the new to find out which endpoints are going to be removed.
  for (auto& host_pair : hosts_map_) {
    if (!hosts_map.contains(host_pair.first)) {
      hosts_removed.push_back(host_pair.second);
    }
  }

  // Update the member data structures.
  hosts_ = std::move(hosts);
  hosts_map_ = std::move(hosts_map);

  // TODO: add stats reporting for number of endpoints added, removed, and reused.
  ENVOY_LOG(debug, "Hosts Added: {}, Removed: {}, Reused: {}", hosts_added.size(),
            hosts_removed.size(), hosts_->size() - hosts_added.size());

  // Update the priority set.
  hosts_per_locality_ =
      std::make_shared<Envoy::Upstream::HostsPerLocalityImpl>(std::move(hosts_by_locality), false);
  priority_set_.updateHosts(
      0, HostSetImpl::partitionHosts(hosts_, hosts_per_locality_), {}, hosts_added, hosts_removed,
      server_context_.api().randomGenerator().random(), absl::nullopt, absl::nullopt);
}

ClusterSharedPtr HdsCluster::create() { return nullptr; }

ClusterInfoConstSharedPtr
ProdClusterInfoFactory::createClusterInfo(const CreateClusterInfoParams& params) {
  Envoy::Stats::ScopeSharedPtr scope =
      params.stats_.createScope(fmt::format("cluster.{}.", params.cluster_.name()));

  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      params.server_context_, params.ssl_context_manager_, *scope,
      params.server_context_.clusterManager(), params.server_context_.messageValidationVisitor());

  // TODO(JimmyCYJ): Support SDS for HDS cluster.
  Network::UpstreamTransportSocketFactoryPtr socket_factory = THROW_OR_RETURN_VALUE(
      Upstream::createTransportSocketFactory(params.cluster_, factory_context),
      Network::UpstreamTransportSocketFactoryPtr);
  auto socket_matcher = THROW_OR_RETURN_VALUE(
      TransportSocketMatcherImpl::create(params.cluster_.transport_socket_matches(),
                                         factory_context, socket_factory, *scope),
      std::unique_ptr<TransportSocketMatcherImpl>);

  return std::make_unique<ClusterInfoImpl>(
      params.server_context_.initManager(), params.server_context_, params.cluster_,
      params.bind_config_, params.server_context_.runtime(), std::move(socket_matcher),
      std::move(scope), params.added_via_api_, factory_context);
}

void HdsCluster::initHealthchecks() {
  for (auto& health_check : cluster_.health_checks()) {
    auto health_checker_or_error =
        Upstream::HealthCheckerFactory::create(health_check, *this, server_context_);
    THROW_IF_STATUS_NOT_OK(health_checker_or_error, throw);

    auto health_checker = health_checker_or_error.value();
    health_checkers_.push_back(health_checker);
    health_checkers_map_.insert({health_check, health_checker});
    health_checker->start();
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
                              *hosts_, {}, server_context_.api().randomGenerator().random(),
                              absl::nullopt, absl::nullopt);

    initialized_ = true;
  }
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr&) {}

} // namespace Upstream
} // namespace Envoy
