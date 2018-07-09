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
      runtime_h(runtime), store_stats(stats), ssl_context_manager_(ssl_context_manager),
      secret_manager_(secret_manager), random_(random), dispatcher_(dispatcher) {
  health_check_request_.mutable_node()->MergeFrom(node);
  retry_timer_ = dispatcher.createTimer([this]() -> void { establishNewStream(); });
  response_timer_ = dispatcher.createTimer([this]() -> void { sendHealthCheckRequest(); });
  establishNewStream();
}

void HdsDelegate::setRetryTimer() {
  retry_timer_->enableTimer(std::chrono::milliseconds(RETRY_DELAY_MS));
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

void HdsDelegate::handleFailure() {
  ENVOY_LOG(warn, "HdsDelegate stream/connection failure, will retry in {} ms.", RETRY_DELAY_MS);
  stats_.errors_.inc();
  setRetryTimer();
}

void HdsDelegate::onCreateInitialMetadata(Http::HeaderMap& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) {
  UNREFERENCED_PARAMETER(metadata);
}

void HdsDelegate::onReceiveMessage(
    std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) {

  stats_.requests_.inc();
  ENVOY_LOG(debug, "New health check response message {} ", message->DebugString());
  envoy::api::v2::core::BindConfig bind_config;

  // Create HdsCluster config
  ENVOY_LOG(debug, "Creating HdsCluster config");
  envoy::api::v2::Cluster cluster_config;
  envoy::api::v2::Cluster& cluster_config_r = cluster_config;
  cluster_config_r.set_name("anna");
  cluster_config_r.mutable_connect_timeout()->set_seconds(2);
  cluster_config_r.mutable_http2_protocol_options();
  cluster_config_r.mutable_per_connection_buffer_limit_bytes()->set_value(12345);

  // Add endpoint to cluster
  auto* socket = cluster_config_r.add_hosts()->mutable_socket_address();
  auto endpoint_address = message->mutable_health_check(0)
                              ->mutable_endpoints(0)
                              ->mutable_endpoints(0)
                              ->mutable_address()
                              ->mutable_socket_address()
                              ->address();
  auto endpoint_port = message->mutable_health_check(0)
                           ->mutable_endpoints(0)
                           ->mutable_endpoints(0)
                           ->mutable_address()
                           ->mutable_socket_address()
                           ->port_value();
  socket->set_address(endpoint_address);
  socket->set_port_value(endpoint_port);

  // Add health checker to cluster
  auto* cluster_health_check = cluster_config_r.add_health_checks();
  cluster_health_check->mutable_timeout()->set_seconds(1);
  cluster_health_check->mutable_interval()->set_seconds(1);
  cluster_health_check->mutable_unhealthy_threshold()->set_value(2);
  cluster_health_check->mutable_healthy_threshold()->set_value(2);
  cluster_health_check->mutable_grpc_health_check();
  auto* http_health_check = cluster_health_check->mutable_http_health_check();
  http_health_check->set_use_http2(false);
  http_health_check->set_path("/healthcheck");
  http_health_check->set_service_name("locations");

  ENVOY_LOG(debug, "New cluster config {} ", cluster_config_r.DebugString());

  // Creating HdsCluster
  HdsCluster cluster(runtime_h, cluster_config_r, bind_config, store_stats, ssl_context_manager_,
                     secret_manager_, false);

  // Creating HealthChecker
  Upstream::HealthCheckerSharedPtr health_checker_ptr = Upstream::HealthCheckerFactory::create(
      *cluster_health_check, cluster, runtime_h, random_, dispatcher_);

  // Initializing HealthChecker
  cluster.setHealthChecker(health_checker_ptr);
  bool initialized = false;
  cluster.initialize([&initialized] { initialized = true; });

  // We're done
  stream_->sendMessage(health_check_request_, false);
  stats_.responses_.inc();
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
    ENVOY_LOG(debug, "Putting hosts in place");
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
  health_checker_->addHostCheckCompleteCb(
      [this](HostSharedPtr, HealthTransition changed_state) -> void {
        // If we get a health check completion that resulted in a state change, signal to
        // update the host sets on all threads.
        ENVOY_LOG(debug, "Callback from setting healthchecker");
        if (changed_state == HealthTransition::Changed) {
          reloadHealthyHosts();
        }
      });
}

void HdsCluster::reloadHealthyHosts() {
  // Every time a host changes HC state we cause a full healthy host recalculation which
  // for expensive LBs (ring, subset, etc.) can be quite time consuming. During startup, this
  // can also block worker threads by doing this repeatedly. There is no reason to do this
  // as we will not start taking traffic until we are initialized. By blocking HC updates
  // while initializing we can avoid this.
  if (initialization_complete_callback_ != nullptr) {
    return;
  }

  for (auto& host_set : prioritySet().hostSetsPerPriority()) {
    ENVOY_LOG(debug, "About to update hosts");
    // TODO(htuch): Can we skip these copies by exporting out const shared_ptr from HostSet?
    HostVectorConstSharedPtr hosts_copy(new HostVector(host_set->hosts()));
    HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().clone();
    host_set->updateHosts(
        hosts_copy, createHealthyHostList(host_set->hosts()), hosts_per_locality_copy,
        createHealthyHostLists(host_set->hostsPerLocality()), host_set->localityWeights(), {}, {});
  }
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
  ASSERT(!initialization_started_);
  ASSERT(initialization_complete_callback_ == nullptr);
  initialization_complete_callback_ = callback;
  startPreInit();
}

void HdsCluster::startPreInit() {
  // At this point see if we have a health checker. If so, mark all the hosts unhealthy and then
  // fire update callbacks to start the health checking process.
  if (health_checker_) {
    for (const auto& host : *initial_hosts_) {
      host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
  }
  // Given the current config, only EDS clusters support multiple priorities.
  ASSERT(priority_set_.hostSetsPerPriority().size() == 1);

  auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  auto healthy = createHealthyHostList(*initial_hosts_);
  ENVOY_LOG(debug, "About to update the priority set.");
  first_host_set.updateHosts(initial_hosts_, healthy, HostsPerLocalityImpl::empty(),
                             HostsPerLocalityImpl::empty(), {}, *initial_hosts_, {});
  initial_hosts_ = nullptr;
  onPreInitComplete();
}

void HdsCluster::onPreInitComplete() {
  // Protect against multiple calls.
  if (initialization_started_) {
    return;
  }
  initialization_started_ = true;
  if (health_checker_ && pending_initialize_health_checks_ == 0) {
    for (auto& host_set : prioritySet().hostSetsPerPriority()) {
      pending_initialize_health_checks_ += host_set->hosts().size();
    }
    // TODO(mattklein123): Remove this callback when done.
    health_checker_->addHostCheckCompleteCb([this](HostSharedPtr, HealthTransition) -> void {
      if (pending_initialize_health_checks_ > 0 && --pending_initialize_health_checks_ == 0) {
        finishInitialization();
      }
    });
  }
  if (pending_initialize_health_checks_ == 0) {
    finishInitialization();
  }
}

void HdsCluster::finishInitialization() {
  ASSERT(initialization_complete_callback_ != nullptr);
  ASSERT(initialization_started_);

  // Snap a copy of the completion callback so that we can set it to nullptr to unblock
  // reloadHealthyHosts(). See that function for more info on why we do this.
  auto snapped_callback = initialization_complete_callback_;
  initialization_complete_callback_ = nullptr;

  if (health_checker_ != nullptr) {
    reloadHealthyHosts();
  }

  if (snapped_callback != nullptr) {
    snapped_callback();
  }
  ENVOY_LOG(debug, "Finished init");
}

const Network::Address::InstanceConstSharedPtr
HdsCluster::resolveProtoAddress2(const envoy::api::v2::core::Address& address) {
  try {
    ENVOY_LOG(debug, "In resolver");
    return Network::Address::resolveProtoAddress(address);
  } catch (EnvoyException& e) {
    if (info_->type() == envoy::api::v2::Cluster::STATIC ||
        info_->type() == envoy::api::v2::Cluster::EDS) {
      throw EnvoyException(fmt::format("{}. Consider setting resolver_name or setting cluster type "
                                       "to 'STRICT_DNS' or 'LOGICAL_DNS'",
                                       e.what()));
    }
    throw e;
  }
}

void HdsCluster::setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector) {
  if (outlier_detector) {
    ENVOY_LOG(debug, "There is an outlier detector");
  } else {
    ENVOY_LOG(debug, "There is no outlier detector");
  }
}

} // namespace Upstream
} // namespace Envoy
