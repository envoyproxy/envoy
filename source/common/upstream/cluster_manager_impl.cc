#include "source/common/upstream/cluster_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/xds_resources_delegate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/async_tcp_client.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/null_grpc_mux_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/grpc/async_client_manager_impl.h"
#include "source/common/http/async_client_impl.h"
#include "source/common/http/http1/conn_pool.h"
#include "source/common/http/http2/conn_pool.h"
#include "source/common/http/mixed_conn_pool.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/shadow_writer_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tcp/conn_pool.h"
#include "source/common/upstream/cds_api_impl.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/common/upstream/priority_conn_pool_map_impl.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/http/conn_pool_grid.h"
#include "source/common/http/http3/conn_pool.h"
#include "source/common/quic/client_connection_factory_impl.h"
#endif

namespace Envoy {
namespace Upstream {
namespace {

void addOptionsIfNotNull(Network::Socket::OptionsSharedPtr& options,
                         const Network::Socket::OptionsSharedPtr& to_add) {
  if (to_add != nullptr) {
    Network::Socket::appendOptions(options, to_add);
  }
}

// Helper function to make sure each protocol in expected_protocols is present
// in protocols (only used for an ASSERT in debug builds)
bool contains(const std::vector<Http::Protocol>& protocols,
              const std::vector<Http::Protocol>& expected_protocols) {
  for (auto protocol : expected_protocols) {
    if (std::find(protocols.begin(), protocols.end(), protocol) == protocols.end()) {
      return false;
    }
  }
  return true;
}

absl::optional<Http::HttpServerPropertiesCache::Origin>
getOrigin(const Network::TransportSocketOptionsConstSharedPtr& options, HostConstSharedPtr host) {
  std::string sni = std::string(host->transportSocketFactory().defaultServerNameIndication());
  if (options && options->serverNameOverride().has_value()) {
    sni = options->serverNameOverride().value();
  }
  if (sni.empty() || !host->address() || !host->address()->ip()) {
    return absl::nullopt;
  }
  return {{"https", sni, host->address()->ip()->port()}};
}

bool isBlockingAdsCluster(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                          absl::string_view cluster_name) {
  if (bootstrap.dynamic_resources().has_ads_config()) {
    const auto& ads_config_source = bootstrap.dynamic_resources().ads_config();
    // We only care about EnvoyGrpc, not GoogleGrpc, because we only need to delay ADS mux
    // initialization if it uses an Envoy cluster that needs to be initialized first. We don't
    // depend on the same cluster initialization when opening a gRPC stream for GoogleGrpc.
    return (ads_config_source.grpc_services_size() > 0 &&
            ads_config_source.grpc_services(0).has_envoy_grpc() &&
            ads_config_source.grpc_services(0).envoy_grpc().cluster_name() == cluster_name);
  }
  return false;
}

} // namespace

void ClusterManagerInitHelper::addCluster(ClusterManagerCluster& cm_cluster) {
  // See comments in ClusterManagerImpl::addOrUpdateCluster() for why this is only called during
  // server initialization.
  ASSERT(state_ != State::AllClustersInitialized);

  const auto initialize_cb = [&cm_cluster, this] {
    onClusterInit(cm_cluster);
    cm_cluster.cluster().info()->configUpdateStats().warming_state_.set(0);
  };
  Cluster& cluster = cm_cluster.cluster();

  cluster.info()->configUpdateStats().warming_state_.set(1);
  if (cluster.initializePhase() == Cluster::InitializePhase::Primary) {
    // Remove the previous cluster before the cluster object is destroyed.
    primary_init_clusters_.insert_or_assign(cm_cluster.cluster().info()->name(), &cm_cluster);
    cluster.initialize(initialize_cb);
  } else {
    ASSERT(cluster.initializePhase() == Cluster::InitializePhase::Secondary);
    // Remove the previous cluster before the cluster object is destroyed.
    secondary_init_clusters_.insert_or_assign(cm_cluster.cluster().info()->name(), &cm_cluster);
    if (started_secondary_initialize_) {
      // This can happen if we get a second CDS update that adds new clusters after we have
      // already started secondary init. In this case, just immediately initialize.
      cluster.initialize(initialize_cb);
    }
  }

  ENVOY_LOG(debug, "cm init: adding: cluster={} primary={} secondary={}", cluster.info()->name(),
            primary_init_clusters_.size(), secondary_init_clusters_.size());
}

void ClusterManagerInitHelper::onClusterInit(ClusterManagerCluster& cluster) {
  ASSERT(state_ != State::AllClustersInitialized);
  per_cluster_init_callback_(cluster);
  removeCluster(cluster);
}

void ClusterManagerInitHelper::removeCluster(ClusterManagerCluster& cluster) {
  if (state_ == State::AllClustersInitialized) {
    return;
  }

  // There is a remote edge case where we can remove a cluster via CDS that has not yet been
  // initialized. When called via the remove cluster API this code catches that case.
  absl::flat_hash_map<std::string, ClusterManagerCluster*>* cluster_map;
  if (cluster.cluster().initializePhase() == Cluster::InitializePhase::Primary) {
    cluster_map = &primary_init_clusters_;
  } else {
    ASSERT(cluster.cluster().initializePhase() == Cluster::InitializePhase::Secondary);
    cluster_map = &secondary_init_clusters_;
  }

  // It is possible that the cluster we are removing has already been initialized, and is not
  // present in the initializer map. If so, this is fine as a CDS update may happen for a
  // cluster with the same name. See the case "UpdateAlreadyInitialized" of the
  // target //test/common/upstream:cluster_manager_impl_test.
  auto iter = cluster_map->find(cluster.cluster().info()->name());
  if (iter != cluster_map->end() && iter->second == &cluster) {
    cluster_map->erase(iter);
  }
  ENVOY_LOG(debug, "cm init: init complete: cluster={} primary={} secondary={}",
            cluster.cluster().info()->name(), primary_init_clusters_.size(),
            secondary_init_clusters_.size());
  maybeFinishInitialize();
}

void ClusterManagerInitHelper::initializeSecondaryClusters() {
  started_secondary_initialize_ = true;
  // Cluster::initialize() method can modify the map of secondary_init_clusters_ to remove
  // the item currently being initialized, so we eschew range-based-for and do this complicated
  // dance to increment the iterator before calling initialize.
  for (auto iter = secondary_init_clusters_.begin(); iter != secondary_init_clusters_.end();) {
    ClusterManagerCluster* cluster = iter->second;
    ENVOY_LOG(debug, "initializing secondary cluster {}", iter->first);
    ++iter;
    cluster->cluster().initialize([cluster, this] { onClusterInit(*cluster); });
  }
}

void ClusterManagerInitHelper::maybeFinishInitialize() {
  // Do not do anything if we are still doing the initial static load or if we are waiting for
  // CDS initialize.
  ENVOY_LOG(debug, "maybe finish initialize state: {}", enumToInt(state_));
  if (state_ == State::Loading || state_ == State::WaitingToStartCdsInitialization) {
    return;
  }

  ASSERT(state_ == State::WaitingToStartSecondaryInitialization ||
         state_ == State::CdsInitialized ||
         state_ == State::WaitingForPrimaryInitializationToComplete);
  ENVOY_LOG(debug, "maybe finish initialize primary init clusters empty: {}",
            primary_init_clusters_.empty());
  // If we are still waiting for primary clusters to initialize, do nothing.
  if (!primary_init_clusters_.empty()) {
    return;
  } else if (state_ == State::WaitingForPrimaryInitializationToComplete) {
    state_ = State::WaitingToStartSecondaryInitialization;
    if (primary_clusters_initialized_callback_) {
      primary_clusters_initialized_callback_();
    }
    return;
  }

  // If we are still waiting for secondary clusters to initialize, see if we need to first call
  // initialize on them. This is only done once.
  ENVOY_LOG(debug, "maybe finish initialize secondary init clusters empty: {}",
            secondary_init_clusters_.empty());
  if (!secondary_init_clusters_.empty()) {
    if (!started_secondary_initialize_) {
      ENVOY_LOG(info, "cm init: initializing secondary clusters");
      // If the first CDS response doesn't have any primary cluster, ClusterLoadAssignment
      // should be already paused by CdsApiImpl::onConfigUpdate(). Need to check that to
      // avoid double pause ClusterLoadAssignment.
      Config::ScopedResume maybe_resume_eds_leds_sds;
      if (cm_.adsMux()) {
        const std::vector<std::string> paused_xds_types{
            Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>(),
            Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>(),
            Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()};
        maybe_resume_eds_leds_sds = cm_.adsMux()->pause(paused_xds_types);
      }
      initializeSecondaryClusters();
    }
    return;
  }

  // At this point, if we are doing static init, and we have CDS, start CDS init. Otherwise, move
  // directly to initialized.
  started_secondary_initialize_ = false;
  ENVOY_LOG(debug, "maybe finish initialize cds api ready: {}", cds_ != nullptr);
  if (state_ == State::WaitingToStartSecondaryInitialization && cds_) {
    ENVOY_LOG(info, "cm init: initializing cds");
    state_ = State::WaitingToStartCdsInitialization;
    cds_->initialize();
  } else {
    ENVOY_LOG(info, "cm init: all clusters initialized");
    state_ = State::AllClustersInitialized;
    if (initialized_callback_) {
      initialized_callback_();
    }
  }
}

void ClusterManagerInitHelper::onStaticLoadComplete() {
  ASSERT(state_ == State::Loading);
  // After initialization of primary clusters has completed, transition to
  // waiting for signal to initialize secondary clusters and then CDS.
  state_ = State::WaitingForPrimaryInitializationToComplete;
  maybeFinishInitialize();
}

void ClusterManagerInitHelper::startInitializingSecondaryClusters() {
  ASSERT(state_ == State::WaitingToStartSecondaryInitialization);
  ENVOY_LOG(debug, "continue initializing secondary clusters");
  maybeFinishInitialize();
}

void ClusterManagerInitHelper::setCds(CdsApi* cds) {
  ASSERT(state_ == State::Loading);
  cds_ = cds;
  if (cds_) {
    cds_->setInitializedCb([this]() -> void {
      ASSERT(state_ == State::WaitingToStartCdsInitialization);
      state_ = State::CdsInitialized;
      maybeFinishInitialize();
    });
  }
}

void ClusterManagerInitHelper::setInitializedCb(
    ClusterManager::InitializationCompleteCallback callback) {
  if (state_ == State::AllClustersInitialized) {
    callback();
  } else {
    initialized_callback_ = callback;
  }
}

void ClusterManagerInitHelper::setPrimaryClustersInitializedCb(
    ClusterManager::PrimaryClustersReadyCallback callback) {
  // The callback must be set before or at the `WaitingToStartSecondaryInitialization` state.
  ASSERT(state_ == State::WaitingToStartSecondaryInitialization ||
         state_ == State::WaitingForPrimaryInitializationToComplete || state_ == State::Loading);
  if (state_ == State::WaitingToStartSecondaryInitialization) {
    // This is the case where all clusters are STATIC and without health checking.
    callback();
  } else {
    primary_clusters_initialized_callback_ = callback;
  }
}

ClusterManagerImpl::ClusterManagerImpl(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap, ClusterManagerFactory& factory,
    Server::Configuration::CommonFactoryContext& context, Stats::Store& stats,
    ThreadLocal::Instance& tls, Runtime::Loader& runtime, const LocalInfo::LocalInfo& local_info,
    AccessLog::AccessLogManager& log_manager, Event::Dispatcher& main_thread_dispatcher,
    OptRef<Server::Admin> admin, ProtobufMessage::ValidationContext& validation_context,
    Api::Api& api, Http::Context& http_context, Grpc::Context& grpc_context,
    Router::Context& router_context, Server::Instance& server)
    : server_(server), factory_(factory), runtime_(runtime), stats_(stats), tls_(tls),
      random_(api.randomGenerator()),
      deferred_cluster_creation_(bootstrap.cluster_manager().enable_deferred_cluster_creation()),
      bind_config_(bootstrap.cluster_manager().has_upstream_bind_config()
                       ? absl::make_optional(bootstrap.cluster_manager().upstream_bind_config())
                       : absl::nullopt),
      local_info_(local_info), cm_stats_(generateStats(*stats.rootScope())),
      init_helper_(*this, [this](ClusterManagerCluster& cluster) { onClusterInit(cluster); }),
      time_source_(main_thread_dispatcher.timeSource()), dispatcher_(main_thread_dispatcher),
      http_context_(http_context), validation_context_(validation_context),
      router_context_(router_context), cluster_stat_names_(stats.symbolTable()),
      cluster_config_update_stat_names_(stats.symbolTable()),
      cluster_lb_stat_names_(stats.symbolTable()),
      cluster_endpoint_stat_names_(stats.symbolTable()),
      cluster_load_report_stat_names_(stats.symbolTable()),
      cluster_circuit_breakers_stat_names_(stats.symbolTable()),
      cluster_request_response_size_stat_names_(stats.symbolTable()),
      cluster_timeout_budget_stat_names_(stats.symbolTable()),
      common_lb_config_pool_(
          std::make_shared<SharedPool::ObjectSharedPool<
              const envoy::config::cluster::v3::Cluster::CommonLbConfig, MessageUtil, MessageUtil>>(
              main_thread_dispatcher)),
      shutdown_(false) {
  if (admin.has_value()) {
    config_tracker_entry_ = admin->getConfigTracker().add(
        "clusters", [this](const Matchers::StringMatcher& name_matcher) {
          return dumpClusterConfigs(name_matcher);
        });
  }
  async_client_manager_ = std::make_unique<Grpc::AsyncClientManagerImpl>(
      *this, tls, context, grpc_context.statNames(), bootstrap.grpc_async_client_manager_config());
  const auto& cm_config = bootstrap.cluster_manager();
  if (cm_config.has_outlier_detection()) {
    const std::string event_log_file_path = cm_config.outlier_detection().event_log_path();
    if (!event_log_file_path.empty()) {
      outlier_event_logger_ = std::make_shared<Outlier::EventLoggerImpl>(
          log_manager, event_log_file_path, time_source_);
    }
  }

  // We need to know whether we're zone aware early on, so make sure we do this lookup
  // before we load any clusters.
  if (!cm_config.local_cluster_name().empty()) {
    local_cluster_name_ = cm_config.local_cluster_name();
  }

  // Initialize the XdsResourceDelegate extension, if set on the bootstrap config.
  if (bootstrap.has_xds_delegate_extension()) {
    auto& factory = Config::Utility::getAndCheckFactory<Config::XdsResourcesDelegateFactory>(
        bootstrap.xds_delegate_extension());
    xds_resources_delegate_ = factory.createXdsResourcesDelegate(
        bootstrap.xds_delegate_extension().typed_config(),
        validation_context.dynamicValidationVisitor(), api, main_thread_dispatcher);
  }

  if (bootstrap.has_xds_config_tracker_extension()) {
    auto& tracer_factory = Config::Utility::getAndCheckFactory<Config::XdsConfigTrackerFactory>(
        bootstrap.xds_config_tracker_extension());
    xds_config_tracker_ = tracer_factory.createXdsConfigTracker(
        bootstrap.xds_config_tracker_extension().typed_config(),
        validation_context.dynamicValidationVisitor(), api, main_thread_dispatcher);
  }

  subscription_factory_ = std::make_unique<Config::SubscriptionFactoryImpl>(
      local_info, main_thread_dispatcher, *this, validation_context.dynamicValidationVisitor(), api,
      server, makeOptRefFromPtr(xds_resources_delegate_.get()),
      makeOptRefFromPtr(xds_config_tracker_.get()));
}

absl::Status
ClusterManagerImpl::initialize(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  ASSERT(!initialized_);
  initialized_ = true;

  // Cluster loading happens in two phases: first all the primary clusters are loaded, and then all
  // the secondary clusters are loaded. As it currently stands all non-EDS clusters and EDS which
  // load endpoint definition from file are primary and
  // (REST,GRPC,DELTA_GRPC) EDS clusters are secondary. This two phase
  // loading is done because in v2 configuration each EDS cluster individually sets up a
  // subscription. When this subscription is an API source the cluster will depend on a non-EDS
  // cluster, so the non-EDS clusters must be loaded first.
  auto is_primary_cluster = [](const envoy::config::cluster::v3::Cluster& cluster) -> bool {
    return cluster.type() != envoy::config::cluster::v3::Cluster::EDS ||
           (cluster.type() == envoy::config::cluster::v3::Cluster::EDS &&
            Config::SubscriptionFactory::isPathBasedConfigSource(
                cluster.eds_cluster_config().eds_config().config_source_specifier_case()));
  };
  // Build book-keeping for which clusters are primary. This is useful when we
  // invoke loadCluster() below and it needs the complete set of primaries.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    if (is_primary_cluster(cluster)) {
      primary_clusters_.insert(cluster.name());
    }
  }

  bool has_ads_cluster = false;
  // Load all the primary clusters.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    if (is_primary_cluster(cluster)) {
      const bool required_for_ads = isBlockingAdsCluster(bootstrap, cluster.name());
      has_ads_cluster |= required_for_ads;
      // TODO(abeyad): Consider passing a lambda for a "post-cluster-init" callback, which would
      // include a conditional ads_mux_->start() call, if other uses cases for "post-cluster-init"
      // functionality pops up.
      auto status_or_cluster =
          loadCluster(cluster, MessageUtil::hash(cluster), "", /*added_via_api=*/false,
                      required_for_ads, active_clusters_);
      RETURN_IF_STATUS_NOT_OK(status_or_cluster);
    }
  }

  const auto& dyn_resources = bootstrap.dynamic_resources();

  // Now setup ADS if needed, this might rely on a primary cluster.
  // This is the only point where distinction between delta ADS and state-of-the-world ADS is made.
  // After here, we just have a GrpcMux interface held in ads_mux_, which hides
  // whether the backing implementation is delta or SotW.
  if (dyn_resources.has_ads_config()) {
    Config::CustomConfigValidatorsPtr custom_config_validators =
        std::make_unique<Config::CustomConfigValidatorsImpl>(
            validation_context_.dynamicValidationVisitor(), server_,
            dyn_resources.ads_config().config_validators());

    auto strategy_or_error = Config::Utility::prepareJitteredExponentialBackOffStrategy(
        dyn_resources.ads_config(), random_,
        Envoy::Config::SubscriptionFactory::RetryInitialDelayMs,
        Envoy::Config::SubscriptionFactory::RetryMaxDelayMs);
    THROW_IF_STATUS_NOT_OK(strategy_or_error, throw);
    JitteredExponentialBackOffStrategyPtr backoff_strategy = std::move(strategy_or_error.value());

    const bool use_eds_cache =
        Runtime::runtimeFeatureEnabled("envoy.restart_features.use_eds_cache_for_ads");
    if (dyn_resources.ads_config().api_type() ==
        envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
      absl::Status status = Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      RETURN_IF_NOT_OK(status);
      std::string name;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        name = "envoy.config_mux.delta_grpc_mux_factory";
      } else {
        name = "envoy.config_mux.new_grpc_mux_factory";
      }
      auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
      if (!factory) {
        return absl::InvalidArgumentError(fmt::format("{} not found", name));
      }
      auto factory_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          *async_client_manager_, dyn_resources.ads_config(), *stats_.rootScope(), false);
      THROW_IF_STATUS_NOT_OK(factory_or_error, throw);
      ads_mux_ = factory->create(factory_or_error.value()->createUncachedRawAsyncClient(), nullptr,
                                 dispatcher_, random_, *stats_.rootScope(),
                                 dyn_resources.ads_config(), local_info_,
                                 std::move(custom_config_validators), std::move(backoff_strategy),
                                 makeOptRefFromPtr(xds_config_tracker_.get()), {}, use_eds_cache);
    } else {
      absl::Status status = Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      RETURN_IF_NOT_OK(status);
      auto xds_delegate_opt_ref = makeOptRefFromPtr(xds_resources_delegate_.get());
      std::string name;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        name = "envoy.config_mux.sotw_grpc_mux_factory";
      } else {
        name = "envoy.config_mux.grpc_mux_factory";
      }

      auto* factory = Config::Utility::getFactoryByName<Config::MuxFactory>(name);
      if (!factory) {
        return absl::InvalidArgumentError(fmt::format("{} not found", name));
      }
      auto factory_or_error = Config::Utility::factoryForGrpcApiConfigSource(
          *async_client_manager_, dyn_resources.ads_config(), *stats_.rootScope(), false);
      THROW_IF_STATUS_NOT_OK(factory_or_error, throw);
      ads_mux_ = factory->create(
          factory_or_error.value()->createUncachedRawAsyncClient(), nullptr, dispatcher_, random_,
          *stats_.rootScope(), dyn_resources.ads_config(), local_info_,
          std::move(custom_config_validators), std::move(backoff_strategy),
          makeOptRefFromPtr(xds_config_tracker_.get()), xds_delegate_opt_ref, use_eds_cache);
    }
  } else {
    ads_mux_ = std::make_unique<Config::NullGrpcMuxImpl>();
  }

  // After ADS is initialized, load EDS static clusters as EDS config may potentially need ADS.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // Now load all the secondary clusters.
    if (cluster.type() == envoy::config::cluster::v3::Cluster::EDS &&
        !Config::SubscriptionFactory::isPathBasedConfigSource(
            cluster.eds_cluster_config().eds_config().config_source_specifier_case())) {
      const bool required_for_ads = isBlockingAdsCluster(bootstrap, cluster.name());
      has_ads_cluster |= required_for_ads;
      auto status_or_cluster =
          loadCluster(cluster, MessageUtil::hash(cluster), "", /*added_via_api=*/false,
                      required_for_ads, active_clusters_);
      if (!status_or_cluster.status().ok()) {
        return status_or_cluster.status();
      }
    }
  }

  cm_stats_.cluster_added_.add(bootstrap.static_resources().clusters().size());
  updateClusterCounts();

  absl::optional<ThreadLocalClusterManagerImpl::LocalClusterParams> local_cluster_params;
  if (local_cluster_name_) {
    auto local_cluster = active_clusters_.find(local_cluster_name_.value());
    if (local_cluster == active_clusters_.end()) {
      return absl::InvalidArgumentError(
          fmt::format("local cluster '{}' must be defined", local_cluster_name_.value()));
    }
    local_cluster_params.emplace();
    local_cluster_params->info_ = local_cluster->second->cluster().info();
    local_cluster_params->load_balancer_factory_ = local_cluster->second->loadBalancerFactory();
    local_cluster->second->setAddedOrUpdated();
  }

  // Once the initial set of static bootstrap clusters are created (including the local cluster),
  // we can instantiate the thread local cluster manager.
  tls_.set([this, local_cluster_params](Event::Dispatcher& dispatcher) {
    return std::make_shared<ThreadLocalClusterManagerImpl>(*this, dispatcher, local_cluster_params);
  });

  // We can now potentially create the CDS API once the backing cluster exists.
  if (dyn_resources.has_cds_config() || !dyn_resources.cds_resources_locator().empty()) {
    std::unique_ptr<xds::core::v3::ResourceLocator> cds_resources_locator;
    if (!dyn_resources.cds_resources_locator().empty()) {
      cds_resources_locator =
          std::make_unique<xds::core::v3::ResourceLocator>(THROW_OR_RETURN_VALUE(
              Config::XdsResourceIdentifier::decodeUrl(dyn_resources.cds_resources_locator()),
              xds::core::v3::ResourceLocator));
    }
    cds_api_ = factory_.createCds(dyn_resources.cds_config(), cds_resources_locator.get(), *this);
    init_helper_.setCds(cds_api_.get());
  } else {
    init_helper_.setCds(nullptr);
  }

  // Proceed to add all static bootstrap clusters to the init manager. This will immediately
  // initialize any primary clusters. Post-init processing further initializes any thread
  // aware load balancer and sets up the per-worker host set updates.
  for (auto& cluster : active_clusters_) {
    init_helper_.addCluster(*cluster.second);
  }

  // Potentially move to secondary initialization on the static bootstrap clusters if all primary
  // clusters have already initialized. (E.g., if all static).
  init_helper_.onStaticLoadComplete();

  if (!has_ads_cluster) {
    // There is no ADS cluster, so we won't be starting the ADS mux after a cluster has finished
    // initializing, so we must start ADS here.
    ads_mux_->start();
  }
  return absl::OkStatus();
}

absl::Status ClusterManagerImpl::initializeSecondaryClusters(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  init_helper_.startInitializingSecondaryClusters();

  const auto& cm_config = bootstrap.cluster_manager();
  if (cm_config.has_load_stats_config()) {
    const auto& load_stats_config = cm_config.load_stats_config();

    absl::Status status = Config::Utility::checkTransportVersion(load_stats_config);
    RETURN_IF_NOT_OK(status);
    auto factory_or_error = Config::Utility::factoryForGrpcApiConfigSource(
        *async_client_manager_, load_stats_config, *stats_.rootScope(), false);
    THROW_IF_STATUS_NOT_OK(factory_or_error, throw);
    load_stats_reporter_ = std::make_unique<LoadStatsReporter>(
        local_info_, *this, *stats_.rootScope(),
        factory_or_error.value()->createUncachedRawAsyncClient(), dispatcher_);
  }
  return absl::OkStatus();
}

ClusterManagerStats ClusterManagerImpl::generateStats(Stats::Scope& scope) {
  const std::string final_prefix = "cluster_manager.";
  return {ALL_CLUSTER_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix))};
}

ThreadLocalClusterManagerStats
ClusterManagerImpl::ThreadLocalClusterManagerImpl::generateStats(Stats::Scope& scope,
                                                                 const std::string& thread_name) {
  const std::string final_prefix = absl::StrCat("thread_local_cluster_manager.", thread_name);
  return {ALL_THREAD_LOCAL_CLUSTER_MANAGER_STATS(POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void ClusterManagerImpl::onClusterInit(ClusterManagerCluster& cm_cluster) {
  // This routine is called when a cluster has finished initializing. The cluster has not yet
  // been setup for cross-thread updates to avoid needless updates during initialization. The order
  // of operations here is important. We start by initializing the thread aware load balancer if
  // needed. This must happen first so cluster updates are heard first by the load balancer.
  // Also, it assures that all of clusters which this function is called should be always active.
  auto& cluster = cm_cluster.cluster();
  auto cluster_data = warming_clusters_.find(cluster.info()->name());
  // We have a situation that clusters will be immediately active, such as static and primary
  // cluster. So we must have this prevention logic here.
  if (cluster_data != warming_clusters_.end()) {
    clusterWarmingToActive(cluster.info()->name());
    updateClusterCounts();
  }
  cluster_data = active_clusters_.find(cluster.info()->name());

  if (cluster_data->second->thread_aware_lb_ != nullptr) {
    THROW_IF_NOT_OK(cluster_data->second->thread_aware_lb_->initialize());
  }

  // Now setup for cross-thread updates.
  // This is used by cluster types such as EDS clusters to drain the connection pools of removed
  // hosts.
  cluster_data->second->member_update_cb_ = cluster.prioritySet().addMemberUpdateCb(
      [&cluster, this](const HostVector&, const HostVector& hosts_removed) -> absl::Status {
        if (cluster.info()->lbConfig().close_connections_on_host_set_change()) {
          for (const auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
            // This will drain all tcp and http connection pools.
            postThreadLocalRemoveHosts(cluster, host_set->hosts());
          }
        } else {
          // TODO(snowp): Should this be subject to merge windows?

          // Whenever hosts are removed from the cluster, we make each TLS cluster drain it's
          // connection pools for the removed hosts. If `close_connections_on_host_set_change` is
          // enabled, this case will be covered by first `if` statement, where all
          // connection pools are drained.
          if (!hosts_removed.empty()) {
            postThreadLocalRemoveHosts(cluster, hosts_removed);
          }
        }
        return absl::OkStatus();
      });

  // This is used by cluster types such as EDS clusters to update the cluster
  // without draining the cluster.
  cluster_data->second->priority_update_cb_ = cluster.prioritySet().addPriorityUpdateCb(
      [&cm_cluster, this](uint32_t priority, const HostVector& hosts_added,
                          const HostVector& hosts_removed) {
        // This fires when a cluster is about to have an updated member set. We need to send this
        // out to all of the thread local configurations.

        // Should we save this update and merge it with other updates?
        //
        // Note that we can only _safely_ merge updates that have no added/removed hosts. That is,
        // only those updates that signal a change in host healthcheck state, weight or metadata.
        //
        // We've discussed merging updates related to hosts being added/removed, but it's really
        // tricky to merge those given that downstream consumers of these updates expect to see the
        // full list of updates, not a condensed one. This is because they use the broadcasted
        // HostSharedPtrs within internal maps to track hosts. If we fail to broadcast the entire
        // list of removals, these maps will leak those HostSharedPtrs.
        //
        // See https://github.com/envoyproxy/envoy/pull/3941 for more context.
        bool scheduled = false;
        const auto merge_timeout = PROTOBUF_GET_MS_OR_DEFAULT(
            cm_cluster.cluster().info()->lbConfig(), update_merge_window, 1000);
        // Remember: we only merge updates with no adds/removes — just hc/weight/metadata changes.
        const bool is_mergeable = hosts_added.empty() && hosts_removed.empty();

        if (merge_timeout > 0) {
          // If this is not mergeable, we should cancel any scheduled updates since
          // we'll deliver it immediately.
          scheduled = scheduleUpdate(cm_cluster, priority, is_mergeable, merge_timeout);
        }

        // If an update was not scheduled for later, deliver it immediately.
        if (!scheduled) {
          cm_stats_.cluster_updated_.inc();
          postThreadLocalClusterUpdate(
              cm_cluster, ThreadLocalClusterUpdateParams(priority, hosts_added, hosts_removed));
        }
        return absl::OkStatus();
      });

  // Finally, post updates cross-thread so the per-thread load balancers are ready. First we
  // populate any update information that may be available after cluster init.
  ThreadLocalClusterUpdateParams params;
  for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    if (host_set->hosts().empty()) {
      continue;
    }
    params.per_priority_update_params_.emplace_back(host_set->priority(), host_set->hosts(),
                                                    HostVector{});
  }
  // NOTE: In all cases *other* than the local cluster, this is when a cluster is added/updated
  // The local cluster must currently be statically defined and must exist prior to other
  // clusters being added/updated. We could gate the below update on hosts being available on
  // the cluster or the cluster not already existing, but the special logic is not worth it.
  postThreadLocalClusterUpdate(cm_cluster, std::move(params));
}

bool ClusterManagerImpl::scheduleUpdate(ClusterManagerCluster& cluster, uint32_t priority,
                                        bool mergeable, const uint64_t timeout) {
  // Find pending updates for this cluster.
  auto& updates_by_prio = updates_map_[cluster.cluster().info()->name()];
  if (!updates_by_prio) {
    updates_by_prio = std::make_unique<PendingUpdatesByPriorityMap>();
  }

  // Find pending updates for this priority.
  auto& updates = (*updates_by_prio)[priority];
  if (!updates) {
    updates = std::make_unique<PendingUpdates>();
  }

  // Has an update_merge_window gone by since the last update? If so, don't schedule
  // the update so it can be applied immediately. Ditto if this is not a mergeable update.
  const auto delta = time_source_.monotonicTime() - updates->last_updated_;
  const uint64_t delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
  const bool out_of_merge_window = delta_ms > timeout;
  if (out_of_merge_window || !mergeable) {
    // If there was a pending update, we cancel the pending merged update.
    //
    // Note: it's possible that even though we are outside of a merge window (delta_ms > timeout),
    // a timer is enabled. This race condition is fine, since we'll disable the timer here and
    // deliver the update immediately.

    // Why wasn't the update scheduled for later delivery? We keep some stats that are helpful
    // to understand why merging did not happen. There's 2 things we are tracking here:

    // 1) Was this update out of a merge window?
    if (mergeable && out_of_merge_window) {
      cm_stats_.update_out_of_merge_window_.inc();
    }

    // 2) Were there previous updates that we are cancelling (and delivering immediately)?
    if (updates->disableTimer()) {
      cm_stats_.update_merge_cancelled_.inc();
    }

    updates->last_updated_ = time_source_.monotonicTime();
    return false;
  }

  // If there's no timer, create one.
  if (updates->timer_ == nullptr) {
    updates->timer_ = dispatcher_.createTimer([this, &cluster, priority, &updates]() -> void {
      applyUpdates(cluster, priority, *updates);
    });
  }

  // Ensure there's a timer set to deliver these updates.
  if (!updates->timer_->enabled()) {
    updates->enableTimer(timeout);
  }

  return true;
}

void ClusterManagerImpl::applyUpdates(ClusterManagerCluster& cluster, uint32_t priority,
                                      PendingUpdates& updates) {
  // Deliver pending updates.

  // Remember that these merged updates are _only_ for updates related to
  // HC/weight/metadata changes. That's why added/removed are empty. All
  // adds/removals were already immediately broadcasted.
  static const HostVector hosts_added;
  static const HostVector hosts_removed;

  postThreadLocalClusterUpdate(
      cluster, ThreadLocalClusterUpdateParams(priority, hosts_added, hosts_removed));

  cm_stats_.cluster_updated_via_merge_.inc();
  updates.last_updated_ = time_source_.monotonicTime();
}

bool ClusterManagerImpl::addOrUpdateCluster(const envoy::config::cluster::v3::Cluster& cluster,
                                            const std::string& version_info) {
  // First we need to see if this new config is new or an update to an existing dynamic cluster.
  // We don't allow updates to statically configured clusters in the main configuration. We check
  // both the warming clusters and the active clusters to see if we need an update or the update
  // should be blocked.
  const std::string& cluster_name = cluster.name();
  const auto existing_active_cluster = active_clusters_.find(cluster_name);
  const auto existing_warming_cluster = warming_clusters_.find(cluster_name);
  const uint64_t new_hash = MessageUtil::hash(cluster);
  if (existing_warming_cluster != warming_clusters_.end()) {
    // If the cluster is the same as the warming cluster of the same name, block the update.
    if (existing_warming_cluster->second->blockUpdate(new_hash)) {
      return false;
    }
    // NB: https://github.com/envoyproxy/envoy/issues/14598
    // Always proceed if the cluster is different from the existing warming cluster.
  } else if (existing_active_cluster != active_clusters_.end() &&
             existing_active_cluster->second->blockUpdate(new_hash)) {
    // If there's no warming cluster of the same name, and if the cluster is the same as the active
    // cluster of the same name, block the update.
    return false;
  }

  if (existing_active_cluster != active_clusters_.end() ||
      existing_warming_cluster != warming_clusters_.end()) {
    if (existing_active_cluster != active_clusters_.end()) {
      // The following init manager remove call is a NOP in the case we are already initialized.
      // It's just kept here to avoid additional logic.
      init_helper_.removeCluster(*existing_active_cluster->second);
    }
    cm_stats_.cluster_modified_.inc();
  } else {
    cm_stats_.cluster_added_.inc();
  }

  // There are two discrete paths here depending on when we are adding/updating a cluster.
  // 1) During initial server load we use the init manager which handles complex logic related to
  //    primary/secondary init, static/CDS init, warming all clusters, etc.
  // 2) After initial server load, we handle warming independently for each cluster in the warming
  //    map.
  // Note: It's likely possible that all warming logic could be centralized in the init manager, but
  //       a decision was made to split the logic given how complex the init manager already is. In
  //       the future we may decide to undergo a refactor to unify the logic but the effort/risk to
  //       do that right now does not seem worth it given that the logic is generally pretty clean
  //       and easy to understand.
  const bool all_clusters_initialized =
      init_helper_.state() == ClusterManagerInitHelper::State::AllClustersInitialized;
  // Preserve the previous cluster data to avoid early destroy. The same cluster should be added
  // before destroy to avoid early initialization complete.
  auto status_or_cluster = loadCluster(cluster, new_hash, version_info, /*added_via_api=*/true,
                                       /*required_for_ads=*/false, warming_clusters_);
  THROW_IF_STATUS_NOT_OK(status_or_cluster, throw);
  const ClusterDataPtr previous_cluster = std::move(status_or_cluster.value());
  auto& cluster_entry = warming_clusters_.at(cluster_name);
  cluster_entry->cluster_->info()->configUpdateStats().warming_state_.set(1);
  if (!all_clusters_initialized) {
    ENVOY_LOG(debug, "add/update cluster {} during init", cluster_name);
    init_helper_.addCluster(*cluster_entry);
  } else {
    ENVOY_LOG(debug, "add/update cluster {} starting warming", cluster_name);
    cluster_entry->cluster_->initialize([this, cluster_name] {
      ENVOY_LOG(debug, "warming cluster {} complete", cluster_name);
      auto state_changed_cluster_entry = warming_clusters_.find(cluster_name);
      state_changed_cluster_entry->second->cluster_->info()->configUpdateStats().warming_state_.set(
          0);
      onClusterInit(*state_changed_cluster_entry->second);
    });
  }

  return true;
}

void ClusterManagerImpl::clusterWarmingToActive(const std::string& cluster_name) {
  auto warming_it = warming_clusters_.find(cluster_name);
  ASSERT(warming_it != warming_clusters_.end());

  // If the cluster is being updated, we need to cancel any pending merged updates.
  // Otherwise, applyUpdates() will fire with a dangling cluster reference.
  updates_map_.erase(cluster_name);

  active_clusters_[cluster_name] = std::move(warming_it->second);
  warming_clusters_.erase(warming_it);
}

bool ClusterManagerImpl::removeCluster(const std::string& cluster_name) {
  bool removed = false;
  auto existing_active_cluster = active_clusters_.find(cluster_name);
  if (existing_active_cluster != active_clusters_.end() &&
      existing_active_cluster->second->added_via_api_) {
    removed = true;
    init_helper_.removeCluster(*existing_active_cluster->second);
    active_clusters_.erase(existing_active_cluster);

    ENVOY_LOG(debug, "removing cluster {}", cluster_name);
    tls_.runOnAllThreads([cluster_name](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
      ASSERT(cluster_manager->thread_local_clusters_.contains(cluster_name) ||
             cluster_manager->thread_local_deferred_clusters_.contains(cluster_name));
      ENVOY_LOG(debug, "removing TLS cluster {}", cluster_name);
      for (auto cb_it = cluster_manager->update_callbacks_.begin();
           cb_it != cluster_manager->update_callbacks_.end();) {
        // The current callback may remove itself from the list, so a handle for
        // the next item is fetched before calling the callback.
        auto curr_cb_it = cb_it;
        ++cb_it;
        (*curr_cb_it)->onClusterRemoval(cluster_name);
      }
      cluster_manager->thread_local_clusters_.erase(cluster_name);
      cluster_manager->thread_local_deferred_clusters_.erase(cluster_name);
      cluster_manager->local_stats_.clusters_inflated_.set(
          cluster_manager->thread_local_clusters_.size());
    });
    cluster_initialization_map_.erase(cluster_name);
  }

  auto existing_warming_cluster = warming_clusters_.find(cluster_name);
  if (existing_warming_cluster != warming_clusters_.end() &&
      existing_warming_cluster->second->added_via_api_) {
    removed = true;
    init_helper_.removeCluster(*existing_warming_cluster->second);
    warming_clusters_.erase(existing_warming_cluster);
    ENVOY_LOG(info, "removing warming cluster {}", cluster_name);
  }

  if (removed) {
    cm_stats_.cluster_removed_.inc();
    updateClusterCounts();
    // Cancel any pending merged updates.
    updates_map_.erase(cluster_name);
  }

  return removed;
}

absl::StatusOr<ClusterManagerImpl::ClusterDataPtr>
ClusterManagerImpl::loadCluster(const envoy::config::cluster::v3::Cluster& cluster,
                                const uint64_t cluster_hash, const std::string& version_info,
                                bool added_via_api, const bool required_for_ads,
                                ClusterMap& cluster_map) {
  absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>
      new_cluster_pair_or_error =
          factory_.clusterFromProto(cluster, *this, outlier_event_logger_, added_via_api);

  if (!new_cluster_pair_or_error.ok()) {
    return absl::InvalidArgumentError(std::string(new_cluster_pair_or_error.status().message()));
  }
  auto& new_cluster = new_cluster_pair_or_error->first;
  auto& lb = new_cluster_pair_or_error->second;
  Cluster& cluster_reference = *new_cluster;

  const auto cluster_info = cluster_reference.info();

  if (!added_via_api) {
    if (cluster_map.find(cluster_info->name()) != cluster_map.end()) {
      return absl::InvalidArgumentError(
          fmt::format("cluster manager: duplicate cluster '{}'", cluster_info->name()));
    }
  }

  // Check if the cluster provided load balancing policy is used. We need handle it as special
  // case.
  TypedLoadBalancerFactory& typed_lb_factory = cluster_info->loadBalancerFactory();
  const bool cluster_provided_lb =
      typed_lb_factory.name() == "envoy.load_balancing_policies.cluster_provided";

  if (cluster_provided_lb && lb == nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("cluster manager: cluster provided LB specified but cluster "
                    "'{}' did not provide one. Check cluster documentation.",
                    cluster_info->name()));
  }
  if (!cluster_provided_lb && lb != nullptr) {
    return absl::InvalidArgumentError(
        fmt::format("cluster manager: cluster provided LB not specified but cluster "
                    "'{}' provided one. Check cluster documentation.",
                    cluster_info->name()));
  }

  if (new_cluster->healthChecker() != nullptr) {
    new_cluster->healthChecker()->addHostCheckCompleteCb(
        [this](HostSharedPtr host, HealthTransition changed_state, HealthState) {
          if (changed_state == HealthTransition::Changed &&
              host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
            postThreadLocalHealthFailure(host);
          }
        });
  }

  if (new_cluster->outlierDetector() != nullptr) {
    new_cluster->outlierDetector()->addChangedStateCb([this](HostSharedPtr host) {
      if (host->healthFlagGet(Host::HealthFlag::FAILED_OUTLIER_CHECK)) {
        ENVOY_LOG_EVENT(debug, "outlier_detection_ejection",
                        "host {} in cluster {} was ejected by the outlier detector",
                        host->address()->asStringView(), host->cluster().name());
        postThreadLocalHealthFailure(host);
      }
    });
  }
  ClusterDataPtr result;
  auto cluster_entry_it = cluster_map.find(cluster_info->name());
  if (cluster_entry_it != cluster_map.end()) {
    result = std::exchange(cluster_entry_it->second,
                           std::make_unique<ClusterData>(cluster, cluster_hash, version_info,
                                                         added_via_api, required_for_ads,
                                                         std::move(new_cluster), time_source_));
  } else {
    bool inserted = false;
    std::tie(cluster_entry_it, inserted) = cluster_map.emplace(
        cluster_info->name(),
        std::make_unique<ClusterData>(cluster, cluster_hash, version_info, added_via_api,
                                      required_for_ads, std::move(new_cluster), time_source_));
    ASSERT(inserted);
  }

  if (cluster_provided_lb) {
    cluster_entry_it->second->thread_aware_lb_ = std::move(lb);
  } else {
    cluster_entry_it->second->thread_aware_lb_ =
        typed_lb_factory.create(cluster_info->loadBalancerConfig(), *cluster_info,
                                cluster_reference.prioritySet(), runtime_, random_, time_source_);
  }

  updateClusterCounts();
  return result;
}

void ClusterManagerImpl::updateClusterCounts() {
  // This if/else block implements a control flow mechanism that can be used by an ADS
  // implementation to properly sequence CDS and RDS updates. It is not enforcing on ADS. ADS can
  // use it to detect when a previously sent cluster becomes warm before sending routes that depend
  // on it. This can improve incidence of HTTP 503 responses from Envoy when a route is used before
  // it's supporting cluster is ready.
  //
  // We achieve that by leaving CDS in the paused state as long as there is at least
  // one cluster in the warming state. This prevents CDS ACK from being sent to ADS.
  // Once cluster is warmed up, CDS is resumed, and ACK is sent to ADS, providing a
  // signal to ADS to proceed with RDS updates.
  // If we're in the middle of shutting down (ads_mux_ already gone) then this is irrelevant.
  const bool all_clusters_initialized =
      init_helper_.state() == ClusterManagerInitHelper::State::AllClustersInitialized;
  if (all_clusters_initialized && ads_mux_) {
    const auto type_url = Config::getTypeUrl<envoy::config::cluster::v3::Cluster>();
    if (resume_cds_ == nullptr && !warming_clusters_.empty()) {
      resume_cds_ = ads_mux_->pause(type_url);
    } else if (warming_clusters_.empty()) {
      resume_cds_.reset();
    }
  }
  cm_stats_.active_clusters_.set(active_clusters_.size());
  cm_stats_.warming_clusters_.set(warming_clusters_.size());
}

ThreadLocalCluster* ClusterManagerImpl::getThreadLocalCluster(absl::string_view cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager = *tls_;

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry != cluster_manager.thread_local_clusters_.end()) {
    return entry->second.get();
  } else {
    return cluster_manager.initializeClusterInlineIfExists(cluster);
  }
}

void ClusterManagerImpl::maybePreconnect(
    ThreadLocalClusterManagerImpl::ClusterEntry& cluster_entry,
    const ClusterConnectivityState& state,
    std::function<ConnectionPool::Instance*()> pick_preconnect_pool) {
  auto peekahead_ratio = cluster_entry.info()->peekaheadRatio();
  if (peekahead_ratio <= 1.0) {
    return;
  }

  // 3 here is arbitrary. Just as in ConnPoolImplBase::tryCreateNewConnections
  // we want to limit the work which can be done on any given preconnect attempt.
  for (int i = 0; i < 3; ++i) {
    // See if adding this one new connection
    // would put the cluster over desired capacity. If so, stop preconnecting.
    //
    // We anticipate the incoming stream here, because maybePreconnect is called
    // before a new stream is established.
    if (!ConnectionPool::ConnPoolImplBase::shouldConnect(
            state.pending_streams_, state.active_streams_,
            state.connecting_and_connected_stream_capacity_, peekahead_ratio, true)) {
      return;
    }
    ConnectionPool::Instance* preconnect_pool = pick_preconnect_pool();
    if (!preconnect_pool || !preconnect_pool->maybePreconnect(peekahead_ratio)) {
      // Given that the next preconnect pick may be entirely different, we could
      // opt to try again even if the first preconnect fails. Err on the side of
      // caution and wait for the next attempt.
      return;
    }
  }
}

absl::optional<HttpPoolData>
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::httpConnPool(
    ResourcePriority priority, absl::optional<Http::Protocol> protocol,
    LoadBalancerContext* context) {
  // Select a host and create a connection pool for it if it does not already exist.
  auto pool = httpConnPoolImpl(priority, protocol, context, false);
  if (pool == nullptr) {
    return absl::nullopt;
  }

  HttpPoolData data(
      [this, priority, protocol, context]() -> void {
        // Now that a new stream is being established, attempt to preconnect.
        maybePreconnect(*this, parent_.cluster_manager_state_,
                        [this, &priority, &protocol, &context]() {
                          return httpConnPoolImpl(priority, protocol, context, true);
                        });
      },
      pool);
  return data;
}

absl::optional<TcpPoolData>
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConnPool(
    ResourcePriority priority, LoadBalancerContext* context) {
  // Select a host and create a connection pool for it if it does not already exist.
  auto pool = tcpConnPoolImpl(priority, context, false);
  if (pool == nullptr) {
    return absl::nullopt;
  }

  TcpPoolData data(
      [this, priority, context]() -> void {
        maybePreconnect(*this, parent_.cluster_manager_state_, [this, &priority, &context]() {
          return tcpConnPoolImpl(priority, context, true);
        });
      },
      pool);
  return data;
}

void ClusterManagerImpl::drainConnections(const std::string& cluster,
                                          DrainConnectionsHostPredicate predicate) {
  ENVOY_LOG_EVENT(debug, "drain_connections_call", "drainConnections called for cluster {}",
                  cluster);
  tls_.runOnAllThreads([cluster, predicate](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    auto cluster_entry = cluster_manager->thread_local_clusters_.find(cluster);
    if (cluster_entry != cluster_manager->thread_local_clusters_.end()) {
      cluster_entry->second->drainConnPools(
          predicate, ConnectionPool::DrainBehavior::DrainExistingConnections);
    }
  });
}

void ClusterManagerImpl::drainConnections(DrainConnectionsHostPredicate predicate) {
  ENVOY_LOG_EVENT(debug, "drain_connections_call_for_all_clusters",
                  "drainConnections called for all clusters");
  tls_.runOnAllThreads([predicate](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    for (const auto& cluster_entry : cluster_manager->thread_local_clusters_) {
      cluster_entry.second->drainConnPools(predicate,
                                           ConnectionPool::DrainBehavior::DrainExistingConnections);
    }
  });
}

absl::Status ClusterManagerImpl::checkActiveStaticCluster(const std::string& cluster) {
  const auto& it = active_clusters_.find(cluster);
  if (it == active_clusters_.end()) {
    return absl::InvalidArgumentError(fmt::format("Unknown gRPC client cluster '{}'", cluster));
  }
  if (it->second->added_via_api_) {
    return absl::InvalidArgumentError(
        fmt::format("gRPC client cluster '{}' is not static", cluster));
  }
  return absl::OkStatus();
}

void ClusterManagerImpl::postThreadLocalRemoveHosts(const Cluster& cluster,
                                                    const HostVector& hosts_removed) {
  // Drain the connection pools for the given hosts. For deferred clusters have
  // been created.
  tls_.runOnAllThreads([name = cluster.info()->name(),
                        hosts_removed](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    cluster_manager->removeHosts(name, hosts_removed);
  });
}

bool ClusterManagerImpl::deferralIsSupportedForCluster(
    const ClusterInfoConstSharedPtr& info) const {
  if (!deferred_cluster_creation_) {
    return false;
  }

  // Certain cluster types are unsupported for deferred initialization.
  // We need to check both the `clusterType()` (preferred) falling back to
  // the `type()` due to how custom clusters were added leveraging an any
  // config.
  if (auto custom_cluster_type = info->clusterType(); custom_cluster_type.has_value()) {
    // TODO(kbaichoo): make it configurable what custom types are supported?
    static const std::array<std::string, 4> supported_well_known_cluster_types = {
        "envoy.clusters.aggregate", "envoy.cluster.eds", "envoy.clusters.redis",
        "envoy.cluster.static"};
    if (std::find(supported_well_known_cluster_types.begin(),
                  supported_well_known_cluster_types.end(),
                  custom_cluster_type->name()) == supported_well_known_cluster_types.end()) {
      return false;
    }

  } else {
    // Check DiscoveryType instead.
    static constexpr std::array<envoy::config::cluster::v3::Cluster::DiscoveryType, 2>
        supported_cluster_types = {envoy::config::cluster::v3::Cluster::EDS,
                                   envoy::config::cluster::v3::Cluster::STATIC};
    if (std::find(supported_cluster_types.begin(), supported_cluster_types.end(), info->type()) ==
        supported_cluster_types.end()) {
      return false;
    }
  }

  return true;
}

void ClusterManagerImpl::postThreadLocalClusterUpdate(ClusterManagerCluster& cm_cluster,
                                                      ThreadLocalClusterUpdateParams&& params) {
  bool add_or_update_cluster = false;
  if (!cm_cluster.addedOrUpdated()) {
    add_or_update_cluster = true;
    cm_cluster.setAddedOrUpdated();
  }

  LoadBalancerFactorySharedPtr load_balancer_factory;
  if (add_or_update_cluster) {
    load_balancer_factory = cm_cluster.loadBalancerFactory();
  }

  for (auto& per_priority : params.per_priority_update_params_) {
    const auto& host_set =
        cm_cluster.cluster().prioritySet().hostSetsPerPriority()[per_priority.priority_];
    per_priority.update_hosts_params_ = HostSetImpl::updateHostsParams(*host_set);
    per_priority.locality_weights_ = host_set->localityWeights();
    per_priority.weighted_priority_health_ = host_set->weightedPriorityHealth();
    per_priority.overprovisioning_factor_ = host_set->overprovisioningFactor();
  }

  HostMapConstSharedPtr host_map = cm_cluster.cluster().prioritySet().crossPriorityHostMap();

  pending_cluster_creations_.erase(cm_cluster.cluster().info()->name());

  const UnitFloat drop_overload = cm_cluster.cluster().dropOverload();
  // Populate the cluster initialization object based on this update.
  ClusterInitializationObjectConstSharedPtr cluster_initialization_object =
      addOrUpdateClusterInitializationObjectIfSupported(
          params, cm_cluster.cluster().info(), load_balancer_factory, host_map, drop_overload);

  tls_.runOnAllThreads([info = cm_cluster.cluster().info(), params = std::move(params),
                        add_or_update_cluster, load_balancer_factory, map = std::move(host_map),
                        cluster_initialization_object = std::move(cluster_initialization_object),
                        drop_overload](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    ASSERT(cluster_manager.has_value(),
           "Expected the ThreadLocalClusterManager to be set during ClusterManagerImpl creation.");

    // Cluster Manager here provided by the particular thread, it will provide
    // this allowing to make the relevant change.
    if (const bool defer_unused_clusters =
            cluster_initialization_object != nullptr &&
            !cluster_manager->thread_local_clusters_.contains(info->name()) &&
            !Envoy::Thread::MainThread::isMainThread();
        defer_unused_clusters) {
      // Save the cluster initialization object.
      ENVOY_LOG(debug, "Deferring add or update for TLS cluster {}", info->name());
      cluster_manager->thread_local_deferred_clusters_[info->name()] =
          cluster_initialization_object;

      // Invoke similar logic of onClusterAddOrUpdate.
      ThreadLocalClusterCommand command = [&cluster_manager,
                                           cluster_name = info->name()]() -> ThreadLocalCluster& {
        // If we have multiple callbacks only the first one needs to use the
        // command to initialize the cluster.
        auto existing_cluster_entry = cluster_manager->thread_local_clusters_.find(cluster_name);
        if (existing_cluster_entry != cluster_manager->thread_local_clusters_.end()) {
          return *existing_cluster_entry->second;
        }

        auto* cluster_entry = cluster_manager->initializeClusterInlineIfExists(cluster_name);
        ASSERT(cluster_entry != nullptr, "Deferred clusters initiailization should not fail.");
        return *cluster_entry;
      };
      for (auto cb_it = cluster_manager->update_callbacks_.begin();
           cb_it != cluster_manager->update_callbacks_.end();) {
        // The current callback may remove itself from the list, so a handle for
        // the next item is fetched before calling the callback.
        auto curr_cb_it = cb_it;
        ++cb_it;
        (*curr_cb_it)->onClusterAddOrUpdate(info->name(), command);
      }

    } else {
      // Broadcast
      ThreadLocalClusterManagerImpl::ClusterEntry* new_cluster = nullptr;
      if (add_or_update_cluster) {
        if (cluster_manager->thread_local_clusters_.contains(info->name())) {
          ENVOY_LOG(debug, "updating TLS cluster {}", info->name());
        } else {
          ENVOY_LOG(debug, "adding TLS cluster {}", info->name());
        }

        new_cluster = new ThreadLocalClusterManagerImpl::ClusterEntry(*cluster_manager, info,
                                                                      load_balancer_factory);
        cluster_manager->thread_local_clusters_[info->name()].reset(new_cluster);
        cluster_manager->local_stats_.clusters_inflated_.set(
            cluster_manager->thread_local_clusters_.size());
      }

      if (cluster_manager->thread_local_clusters_[info->name()]) {
        cluster_manager->thread_local_clusters_[info->name()]->setDropOverload(drop_overload);
      }
      for (const auto& per_priority : params.per_priority_update_params_) {
        cluster_manager->updateClusterMembership(
            info->name(), per_priority.priority_, per_priority.update_hosts_params_,
            per_priority.locality_weights_, per_priority.hosts_added_, per_priority.hosts_removed_,
            per_priority.weighted_priority_health_, per_priority.overprovisioning_factor_, map);
      }

      if (new_cluster != nullptr) {
        ThreadLocalClusterCommand command = [&new_cluster]() -> ThreadLocalCluster& {
          return *new_cluster;
        };
        for (auto cb_it = cluster_manager->update_callbacks_.begin();
             cb_it != cluster_manager->update_callbacks_.end();) {
          // The current callback may remove itself from the list, so a handle for
          // the next item is fetched before calling the callback.
          auto curr_cb_it = cb_it;
          ++cb_it;
          (*curr_cb_it)->onClusterAddOrUpdate(info->name(), command);
        }
      }
    }
  });

  // By this time, the main thread has received the cluster initialization update, so we can start
  // the ADS mux if the ADS mux is dependent on this cluster's initialization.
  if (cm_cluster.requiredForAds() && !ads_mux_initialized_) {
    ads_mux_->start();
    ads_mux_initialized_ = true;
  }
}

ClusterManagerImpl::ClusterInitializationObjectConstSharedPtr
ClusterManagerImpl::addOrUpdateClusterInitializationObjectIfSupported(
    const ThreadLocalClusterUpdateParams& params, ClusterInfoConstSharedPtr cluster_info,
    LoadBalancerFactorySharedPtr load_balancer_factory, HostMapConstSharedPtr map,
    UnitFloat drop_overload) {
  if (!deferralIsSupportedForCluster(cluster_info)) {
    return nullptr;
  }

  const std::string& cluster_name = cluster_info->name();
  auto entry = cluster_initialization_map_.find(cluster_name);
  // TODO(kbaichoo): if EDS can be configured via cluster_type() then modify the
  // merging logic below.
  //
  // This method may be called multiple times to create multiple ClusterInitializationObject
  // instances for the same cluster. And before the thread local clusters are actually initialized,
  // the new instances will override the old instances in the work threads. But part of data is be
  // created only once, such as load balancer factory. So we should always to merge the new instance
  // with the old one to keep the latest instance have all necessary data.
  //
  // More specifically, this will happen in the following scenarios for now:
  // 1. EDS clusters: the ClusterLoadAssignment of EDS cluster may be updated multiples before
  //   the thread local cluster is initialized.
  // 2. Clusters in the unit tests: the cluster in the unit test may be updated multiples before
  //   the thread local cluster is initialized by calling 'updateHosts' manually.
  const bool should_merge_with_prior_cluster =
      entry != cluster_initialization_map_.end() && entry->second->cluster_info_ == cluster_info;

  if (should_merge_with_prior_cluster) {
    // We need to copy from an existing Cluster Initialization Object. In
    // particular, only update the params with changed priority.
    auto new_initialization_object = std::make_shared<ClusterInitializationObject>(
        entry->second->per_priority_state_, params, std::move(cluster_info),
        load_balancer_factory == nullptr ? entry->second->load_balancer_factory_
                                         : load_balancer_factory,
        map, drop_overload);
    cluster_initialization_map_[cluster_name] = new_initialization_object;
    return new_initialization_object;
  } else {
    // We need to create a fresh Cluster Initialization Object.
    auto new_initialization_object = std::make_shared<ClusterInitializationObject>(
        params, std::move(cluster_info), load_balancer_factory, map, drop_overload);
    cluster_initialization_map_[cluster_name] = new_initialization_object;
    return new_initialization_object;
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::initializeClusterInlineIfExists(
    absl::string_view cluster) {
  auto entry = thread_local_deferred_clusters_.find(cluster);
  if (entry == thread_local_deferred_clusters_.end()) {
    // Unknown cluster.
    return nullptr;
  }

  // Create the cluster inline.
  const ClusterInitializationObjectConstSharedPtr& initialization_object = entry->second;
  ENVOY_LOG(debug, "initializing TLS cluster {} inline", cluster);
  auto cluster_entry = std::make_unique<ClusterEntry>(
      *this, initialization_object->cluster_info_, initialization_object->load_balancer_factory_);
  ClusterEntry* cluster_entry_ptr = cluster_entry.get();

  thread_local_clusters_[cluster] = std::move(cluster_entry);
  local_stats_.clusters_inflated_.set(thread_local_clusters_.size());

  for (const auto& [_, per_priority] : initialization_object->per_priority_state_) {
    updateClusterMembership(initialization_object->cluster_info_->name(), per_priority.priority_,
                            per_priority.update_hosts_params_, per_priority.locality_weights_,
                            per_priority.hosts_added_, per_priority.hosts_removed_,
                            per_priority.weighted_priority_health_,
                            per_priority.overprovisioning_factor_,
                            initialization_object->cross_priority_host_map_);
  }
  thread_local_clusters_[cluster]->setDropOverload(initialization_object->drop_overload_);

  // Remove the CIO as we've initialized the cluster.
  thread_local_deferred_clusters_.erase(entry);

  return cluster_entry_ptr;
}

ClusterManagerImpl::ClusterInitializationObject::ClusterInitializationObject(
    const ThreadLocalClusterUpdateParams& params, ClusterInfoConstSharedPtr cluster_info,
    LoadBalancerFactorySharedPtr load_balancer_factory, HostMapConstSharedPtr map,
    UnitFloat drop_overload)
    : cluster_info_(std::move(cluster_info)), load_balancer_factory_(load_balancer_factory),
      cross_priority_host_map_(map), drop_overload_(drop_overload) {
  // Copy the update since the map is empty.
  for (const auto& update : params.per_priority_update_params_) {
    per_priority_state_.emplace(update.priority_, update);
  }
}

ClusterManagerImpl::ClusterInitializationObject::ClusterInitializationObject(
    const absl::flat_hash_map<int, ThreadLocalClusterUpdateParams::PerPriority>& per_priority_state,
    const ThreadLocalClusterUpdateParams& update_params, ClusterInfoConstSharedPtr cluster_info,
    LoadBalancerFactorySharedPtr load_balancer_factory, HostMapConstSharedPtr map,
    UnitFloat drop_overload)
    : per_priority_state_(per_priority_state), cluster_info_(std::move(cluster_info)),
      load_balancer_factory_(load_balancer_factory), cross_priority_host_map_(map),
      drop_overload_(drop_overload) {

  // Because EDS Clusters receive the entire ClusterLoadAssignment but only
  // provides the delta we must process the hosts_added and hosts_removed and
  // not simply overwrite with hosts added.
  for (const auto& update : update_params.per_priority_update_params_) {
    auto it = per_priority_state_.find(update.priority_);
    if (it != per_priority_state_.end()) {
      auto& priority_state = it->second;
      // Merge the two per_priorities.
      priority_state.update_hosts_params_ = update.update_hosts_params_;
      priority_state.locality_weights_ = update.locality_weights_;
      priority_state.weighted_priority_health_ = update.weighted_priority_health_;
      priority_state.overprovisioning_factor_ = update.overprovisioning_factor_;

      // Merge the hosts vectors to just have hosts added.
      // Assumes that the old host_added_ is exclusive to new hosts_added_ and
      // new hosts_removed_ only refers to the old hosts_added_.
      ASSERT(priority_state.hosts_removed_.empty(),
             "Cluster Initialization Object should apply hosts "
             "removed updates to hosts_added vector!");

      // TODO(kbaichoo): replace with a more efficient algorithm. For example
      // if the EDS cluster exposed the LoadAssignment we could just merge by
      // overwriting hosts_added.
      if (!update.hosts_removed_.empty()) {
        // Remove all hosts to be removed from the old host_added.
        auto& host_added = priority_state.hosts_added_;
        auto removed_section = std::remove_if(
            host_added.begin(), host_added.end(),
            [hosts_removed = std::cref(update.hosts_removed_)](const HostSharedPtr& ptr) {
              return std::find(hosts_removed.get().begin(), hosts_removed.get().end(), ptr) !=
                     hosts_removed.get().end();
            });
        priority_state.hosts_added_.erase(removed_section, priority_state.hosts_added_.end());
      }

      // Add updated host_added.
      priority_state.hosts_added_.reserve(priority_state.hosts_added_.size() +
                                          update.hosts_added_.size());
      std::copy(update.hosts_added_.begin(), update.hosts_added_.end(),
                std::back_inserter(priority_state.hosts_added_));

    } else {
      // Just copy the new priority.
      per_priority_state_.emplace(update.priority_, update);
    }
  }
}

void ClusterManagerImpl::postThreadLocalHealthFailure(const HostSharedPtr& host) {
  tls_.runOnAllThreads([host](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    cluster_manager->onHostHealthFailure(host);
  });
}

Host::CreateConnectionData ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConn(
    LoadBalancerContext* context) {
  HostConstSharedPtr logical_host = chooseHost(context);
  if (logical_host) {
    auto conn_info = logical_host->createConnection(
        parent_.thread_local_dispatcher_, nullptr,
        context == nullptr ? nullptr : context->upstreamTransportSocketOptions());
    if ((cluster_info_->features() &
         ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) &&
        conn_info.connection_ != nullptr) {
      auto conn_map_iter = parent_.host_tcp_conn_map_.find(logical_host);
      if (conn_map_iter == parent_.host_tcp_conn_map_.end()) {
        conn_map_iter =
            parent_.host_tcp_conn_map_.try_emplace(logical_host, logical_host->acquireHandle())
                .first;
      }
      auto& conn_map = conn_map_iter->second;
      conn_map.connections_.emplace(
          conn_info.connection_.get(),
          std::make_unique<ThreadLocalClusterManagerImpl::TcpConnContainer>(
              parent_, logical_host, *conn_info.connection_));
    }
    return conn_info;
  } else {
    cluster_info_->trafficStats()->upstream_cx_none_healthy_.inc();
    return {nullptr, nullptr};
  }
}

Http::AsyncClient&
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::httpAsyncClient() {
  if (lazy_http_async_client_ == nullptr) {
    lazy_http_async_client_ = std::make_unique<Http::AsyncClientImpl>(
        cluster_info_, parent_.parent_.stats_, parent_.thread_local_dispatcher_, parent_.parent_,
        parent_.parent_.server_.serverFactoryContext(),
        Router::ShadowWriterPtr{new Router::ShadowWriterImpl(parent_.parent_)},
        parent_.parent_.http_context_, parent_.parent_.router_context_);
  }
  return *lazy_http_async_client_;
}

Tcp::AsyncTcpClientPtr
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpAsyncClient(
    LoadBalancerContext* context, Tcp::AsyncTcpClientOptionsConstSharedPtr options) {
  return std::make_unique<Tcp::AsyncTcpClientImpl>(parent_.thread_local_dispatcher_, *this, context,
                                                   options->enable_half_close);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::updateHosts(
    const std::string& name, uint32_t priority,
    PrioritySet::UpdateHostsParams&& update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, uint64_t seed, absl::optional<bool> weighted_priority_health,
    absl::optional<uint32_t> overprovisioning_factor,
    HostMapConstSharedPtr cross_priority_host_map) {
  ENVOY_LOG(debug, "membership update for TLS cluster {} added {} removed {}", name,
            hosts_added.size(), hosts_removed.size());
  priority_set_.updateHosts(priority, std::move(update_hosts_params), std::move(locality_weights),
                            hosts_added, hosts_removed, seed, weighted_priority_health,
                            overprovisioning_factor, std::move(cross_priority_host_map));
  // If an LB is thread aware, create a new worker local LB on membership changes.
  if (lb_factory_ != nullptr && lb_factory_->recreateOnHostChange()) {
    ENVOY_LOG(debug, "re-creating local LB for TLS cluster {}", name);
    lb_ = lb_factory_->create({priority_set_, parent_.local_priority_set_});
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainConnPools(
    const HostVector& hosts_removed) {
  for (const auto& host : hosts_removed) {
    parent_.drainOrCloseConnPools(host, ConnectionPool::DrainBehavior::DrainAndDelete);
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainConnPools() {
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    drainConnPools(host_set->hosts());
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainConnPools(
    DrainConnectionsHostPredicate predicate, ConnectionPool::DrainBehavior behavior) {
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      if (predicate != nullptr && !predicate(*host)) {
        continue;
      }

      parent_.drainOrCloseConnPools(host, behavior);
    }
  }
}

ClusterUpdateCallbacksHandlePtr
ClusterManagerImpl::addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) {
  ThreadLocalClusterManagerImpl& cluster_manager = *tls_;
  return cluster_manager.addClusterUpdateCallbacks(cb);
}

OdCdsApiHandlePtr
ClusterManagerImpl::allocateOdCdsApi(const envoy::config::core::v3::ConfigSource& odcds_config,
                                     OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                                     ProtobufMessage::ValidationVisitor& validation_visitor) {
  // TODO(krnowak): Instead of creating a new handle every time, store the handles internally and
  // return an already existing one if the config or locator matches. Note that this may need a
  // way to clean up the unused handles, so we can close the unnecessary connections.
  auto odcds = OdCdsApiImpl::create(odcds_config, odcds_resources_locator, *this, *this,
                                    *stats_.rootScope(), validation_visitor);
  return OdCdsApiHandleImpl::create(*this, std::move(odcds));
}

ClusterDiscoveryCallbackHandlePtr
ClusterManagerImpl::requestOnDemandClusterDiscovery(OdCdsApiSharedPtr odcds, std::string name,
                                                    ClusterDiscoveryCallbackPtr callback,
                                                    std::chrono::milliseconds timeout) {
  ThreadLocalClusterManagerImpl& cluster_manager = *tls_;

  auto [handle, discovery_in_progress, invoker] =
      cluster_manager.cdm_.addCallback(name, std::move(callback));
  // This check will catch requests for discoveries from this thread only. If other thread
  // requested the same discovery, we will detect it in the main thread later.
  if (discovery_in_progress) {
    ENVOY_LOG(debug,
              "cm odcds: on-demand discovery for cluster {} is already in progress, something else "
              "in thread {} has already requested it",
              name, cluster_manager.thread_local_dispatcher_.name());
    // This worker thread has already requested a discovery of a cluster with this name, so
    // nothing more left to do here.
    //
    // We can't "just" return handle here, because handle is a part of the structured binding done
    // above. So it's not really a ClusterDiscoveryCallbackHandlePtr, but more like
    // ClusterDiscoveryCallbackHandlePtr&, so named return value optimization does not apply here
    // - it needs to be moved.
    return std::move(handle);
  }
  ENVOY_LOG(
      debug,
      "cm odcds: forwarding the on-demand discovery request for cluster {} to the main thread",
      name);
  // This seems to be the first request for discovery of this cluster in this worker thread. Rest
  // of the process may only happen in the main thread.
  dispatcher_.post([this, odcds = std::move(odcds), timeout, name = std::move(name),
                    invoker = std::move(invoker),
                    &thread_local_dispatcher = cluster_manager.thread_local_dispatcher_] {
    // Check for the cluster here too. It might have been added between the time when this closure
    // was posted and when it is being executed.
    if (getThreadLocalCluster(name) != nullptr) {
      ENVOY_LOG(
          debug,
          "cm odcds: the requested cluster {} is already known, posting the callback back to {}",
          name, thread_local_dispatcher.name());
      thread_local_dispatcher.post([invoker = std::move(invoker)] {
        invoker.invokeCallback(ClusterDiscoveryStatus::Available);
      });
      return;
    }

    if (auto it = pending_cluster_creations_.find(name); it != pending_cluster_creations_.end()) {
      ENVOY_LOG(debug, "cm odcds: on-demand discovery for cluster {} is already in progress", name);
      // We already began the discovery process for this cluster, nothing to do. If we got here,
      // it means that it was other worker thread that requested the discovery.
      return;
    }
    // Start the discovery. If the cluster gets discovered, cluster manager will warm it up and
    // invoke the cluster lifecycle callbacks, that will in turn invoke our callback.
    odcds->updateOnDemand(name);
    // Setup the discovery timeout timer to avoid keeping callbacks indefinitely.
    auto timer = dispatcher_.createTimer([this, name] { notifyExpiredDiscovery(name); });
    timer->enableTimer(timeout);
    // Keep odcds handle alive for the duration of the discovery process.
    pending_cluster_creations_.insert(
        {std::move(name), ClusterCreation{std::move(odcds), std::move(timer)}});
  });

  // We can't "just" return handle here, because handle is a part of the structured binding done
  // above. So it's not really a ClusterDiscoveryCallbackHandlePtr, but more like
  // ClusterDiscoveryCallbackHandlePtr&, so named return value optimization does not apply here -
  // it needs to be moved.
  return std::move(handle);
}

void ClusterManagerImpl::notifyMissingCluster(absl::string_view name) {
  ENVOY_LOG(debug, "cm odcds: cluster {} not found during on-demand discovery", name);
  notifyClusterDiscoveryStatus(name, ClusterDiscoveryStatus::Missing);
}

void ClusterManagerImpl::notifyExpiredDiscovery(absl::string_view name) {
  ENVOY_LOG(debug, "cm odcds: on-demand discovery for cluster {} timed out", name);
  notifyClusterDiscoveryStatus(name, ClusterDiscoveryStatus::Timeout);
}

void ClusterManagerImpl::notifyClusterDiscoveryStatus(absl::string_view name,
                                                      ClusterDiscoveryStatus status) {
  auto map_node_handle = pending_cluster_creations_.extract(name);
  if (map_node_handle.empty()) {
    // Not a cluster we are interested in. This may happen when ODCDS
    // receives some cluster name in removed resources field and
    // notifies the cluster manager about it.
    return;
  }
  // Let all the worker threads know that the discovery timed out.
  tls_.runOnAllThreads(
      [name = std::string(name), status](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
        ENVOY_LOG(
            trace,
            "cm cdm: starting processing cluster name {} (status {}) from the expired timer in {}",
            name, enumToInt(status), cluster_manager->thread_local_dispatcher_.name());
        cluster_manager->cdm_.processClusterName(name, status);
      });
}

Config::EdsResourcesCacheOptRef ClusterManagerImpl::edsResourcesCache() {
  // EDS caching is only supported for ADS.
  if (ads_mux_) {
    return ads_mux_->edsResourcesCache();
  }
  return {};
}

ClusterDiscoveryManager
ClusterManagerImpl::createAndSwapClusterDiscoveryManager(std::string thread_name) {
  ThreadLocalClusterManagerImpl& cluster_manager = *tls_;
  ClusterDiscoveryManager cdm(std::move(thread_name), cluster_manager);

  cluster_manager.cdm_.swap(cdm);

  return cdm;
}

ProtobufTypes::MessagePtr
ClusterManagerImpl::dumpClusterConfigs(const Matchers::StringMatcher& name_matcher) {
  auto config_dump = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
  config_dump->set_version_info(cds_api_ != nullptr ? cds_api_->versionInfo() : "");
  for (const auto& active_cluster_pair : active_clusters_) {
    const auto& cluster = *active_cluster_pair.second;
    if (!name_matcher.match(cluster.cluster_config_.name())) {
      continue;
    }
    if (!cluster.added_via_api_) {
      auto& static_cluster = *config_dump->mutable_static_clusters()->Add();
      static_cluster.mutable_cluster()->PackFrom(cluster.cluster_config_);
      TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                            *(static_cluster.mutable_last_updated()));
    } else {
      auto& dynamic_cluster = *config_dump->mutable_dynamic_active_clusters()->Add();
      dynamic_cluster.set_version_info(cluster.version_info_);
      dynamic_cluster.mutable_cluster()->PackFrom(cluster.cluster_config_);
      TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                            *(dynamic_cluster.mutable_last_updated()));
    }
  }

  for (const auto& warming_cluster_pair : warming_clusters_) {
    const auto& cluster = *warming_cluster_pair.second;
    if (!name_matcher.match(cluster.cluster_config_.name())) {
      continue;
    }
    auto& dynamic_cluster = *config_dump->mutable_dynamic_warming_clusters()->Add();
    dynamic_cluster.set_version_info(cluster.version_info_);
    dynamic_cluster.mutable_cluster()->PackFrom(cluster.cluster_config_);
    TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                          *(dynamic_cluster.mutable_last_updated()));
  }

  return config_dump;
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ThreadLocalClusterManagerImpl(
    ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
    const absl::optional<LocalClusterParams>& local_cluster_params)
    : parent_(parent), thread_local_dispatcher_(dispatcher), cdm_(dispatcher.name(), *this),
      local_stats_(generateStats(*parent.stats_.rootScope(), dispatcher.name())) {
  // If local cluster is defined then we need to initialize it first.
  if (local_cluster_params.has_value()) {
    const auto& local_cluster_name = local_cluster_params->info_->name();
    ENVOY_LOG(debug, "adding TLS local cluster {}", local_cluster_name);
    thread_local_clusters_[local_cluster_name] = std::make_unique<ClusterEntry>(
        *this, local_cluster_params->info_, local_cluster_params->load_balancer_factory_);
    local_priority_set_ = &thread_local_clusters_[local_cluster_name]->prioritySet();
    local_stats_.clusters_inflated_.set(thread_local_clusters_.size());
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::~ThreadLocalClusterManagerImpl() {
  // Clear out connection pools as well as the thread local cluster map so that we release all
  // cluster pointers. Currently we have to free all non-local clusters before we free
  // the local cluster. This is because non-local clusters with a zone aware load balancer have a
  // member update callback registered with the local cluster.
  ENVOY_LOG(debug, "shutting down thread local cluster manager");
  destroying_ = true;
  host_http_conn_pool_map_.clear();
  host_tcp_conn_pool_map_.clear();
  ASSERT(host_tcp_conn_map_.empty());
  for (auto& cluster : thread_local_clusters_) {
    if (&cluster.second->prioritySet() != local_priority_set_) {
      cluster.second.reset();
    }
  }
  thread_local_clusters_.clear();

  // Ensure that all pools are completely destructed.
  thread_local_dispatcher_.clearDeferredDeleteList();
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::removeTcpConn(
    const HostConstSharedPtr& host, Network::ClientConnection& connection) {
  auto host_tcp_conn_map_it = host_tcp_conn_map_.find(host);
  ASSERT(host_tcp_conn_map_it != host_tcp_conn_map_.end());
  auto& connections_map = host_tcp_conn_map_it->second.connections_;
  auto it = connections_map.find(&connection);
  ASSERT(it != connections_map.end());
  connection.dispatcher().deferredDelete(std::move(it->second));
  connections_map.erase(it);
  if (connections_map.empty()) {
    host_tcp_conn_map_.erase(host_tcp_conn_map_it);
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::removeHosts(
    const std::string& name, const HostVector& hosts_removed) {
  auto entry = thread_local_clusters_.find(name);
  // The if should only be possible if deferred cluster creation is enabled.
  if (entry == thread_local_clusters_.end()) {
    ASSERT(
        parent_.deferred_cluster_creation_,
        fmt::format("Cannot find ThreadLocalCluster {}, but deferred cluster creation is disabled.",
                    name));
    ASSERT(thread_local_deferred_clusters_.find(name) != thread_local_deferred_clusters_.end(),
           "Cluster with removed host is neither deferred or inflated!");
    return;
  }
  const auto& cluster_entry = entry->second;
  ENVOY_LOG(debug, "removing hosts for TLS cluster {} removed {}", name, hosts_removed.size());

  // We need to go through and purge any connection pools for hosts that got deleted.
  // Even if two hosts actually point to the same address this will be safe, since if a
  // host is readded it will be a different physical HostSharedPtr.
  cluster_entry->drainConnPools(hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, uint32_t priority, PrioritySet::UpdateHostsParams update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, bool weighted_priority_health,
    uint64_t overprovisioning_factor, HostMapConstSharedPtr cross_priority_host_map) {
  ASSERT(thread_local_clusters_.find(name) != thread_local_clusters_.end());
  const auto& cluster_entry = thread_local_clusters_[name];
  cluster_entry->updateHosts(name, priority, std::move(update_hosts_params),
                             std::move(locality_weights), hosts_added, hosts_removed,
                             parent_.random_.random(), weighted_priority_health,
                             overprovisioning_factor, std::move(cross_priority_host_map));
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure(
    const HostSharedPtr& host) {
  if (host->cluster().features() &
      ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) {
    drainOrCloseConnPools(host, absl::nullopt);

    // Close non connection pool TCP connections obtained from tcpConn()
    //
    // TODO(jono): The only remaining user of the non-pooled connections seems to be the statsd
    // TCP client. Perhaps it could be rewritten to use a connection pool, and this code deleted.
    //
    // Each connection will remove itself from the TcpConnectionsMap when it closes, via its
    // Network::ConnectionCallbacks. The last removed tcp conn will remove the TcpConnectionsMap
    // from host_tcp_conn_map_, so do not cache it between iterations.
    //
    // TODO(ggreenway) PERF: If there are a large number of connections, this could take a long
    // time and halt other useful work. Consider breaking up this work. Note that this behavior is
    // noted in the configuration documentation in cluster setting
    // "close_connections_on_host_health_failure". Update the docs if this if this changes.
    while (true) {
      const auto& it = host_tcp_conn_map_.find(host);
      if (it == host_tcp_conn_map_.end()) {
        break;
      }
      TcpConnectionsMap& container = it->second;
      container.connections_.begin()->first->close(
          Network::ConnectionCloseType::NoFlush,
          StreamInfo::LocalCloseReasons::get().NonPooledTcpConnectionHostHealthFailure);
    }
  } else {
    drainOrCloseConnPools(host, ConnectionPool::DrainBehavior::DrainExistingConnections);
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ConnPoolsContainer*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::getHttpConnPoolsContainer(
    const HostConstSharedPtr& host, bool allocate) {
  auto container_iter = host_http_conn_pool_map_.find(host);
  if (container_iter == host_http_conn_pool_map_.end()) {
    if (!allocate) {
      return nullptr;
    }
    container_iter =
        host_http_conn_pool_map_.try_emplace(host, thread_local_dispatcher_, host).first;
  }

  return &container_iter->second;
}

ClusterUpdateCallbacksHandlePtr
ClusterManagerImpl::ThreadLocalClusterManagerImpl::addClusterUpdateCallbacks(
    ClusterUpdateCallbacks& cb) {
  return std::make_unique<ClusterUpdateCallbacksHandleImpl>(cb, update_callbacks_);
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
    const LoadBalancerFactorySharedPtr& lb_factory)
    : parent_(parent), cluster_info_(cluster), lb_factory_(lb_factory),
      override_host_statuses_(HostUtility::createOverrideHostStatus(cluster_info_->lbConfig())) {
  priority_set_.getOrCreateHostSet(0);

  // TODO(mattklein123): Consider converting other LBs over to thread local. All of them could
  // benefit given the healthy panic, locality, and priority calculations that take place.
  ASSERT(lb_factory_ != nullptr);
  lb_ = lb_factory_->create({priority_set_, parent_.local_priority_set_});
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainOrCloseConnPools(
    const HostSharedPtr& host, absl::optional<ConnectionPool::DrainBehavior> drain_behavior) {
  // Drain or close any HTTP connection pool for the host.
  {
    const auto container = getHttpConnPoolsContainer(host);
    if (container != nullptr) {
      container->do_not_delete_ = true;
      if (drain_behavior.has_value()) {
        container->pools_->drainConnections(drain_behavior.value());
      } else {
        // TODO(wbpcode): 'CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE' and 'closeConnections'
        // is only supported for TCP connection pools for now. Use 'DrainExistingConnections'
        // drain here as alternative.
        container->pools_->drainConnections(
            ConnectionPool::DrainBehavior::DrainExistingConnections);
      }
      container->do_not_delete_ = false;

      if (container->pools_->empty()) {
        host_http_conn_pool_map_.erase(host);
      }
    }
  }
  // Drain or close any TCP connection pool for the host.
  {
    const auto container = host_tcp_conn_pool_map_.find(host);
    if (container != host_tcp_conn_pool_map_.end()) {
      // Draining pools or closing connections can cause pool deletion if it becomes
      // idle. Copy `pools_` so that we aren't iterating through a container that
      // gets mutated by callbacks deleting from it.
      std::vector<Tcp::ConnectionPool::Instance*> pools;
      for (const auto& pair : container->second.pools_) {
        pools.push_back(pair.second.get());
      }

      for (auto* pool : pools) {
        if (drain_behavior.has_value()) {
          pool->drainConnections(drain_behavior.value());
        } else {
          pool->closeConnections();
        }
      }
    }
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::~ClusterEntry() {
  // We need to drain all connection pools for the cluster being removed. Then we can remove the
  // cluster.
  //
  // TODO(mattklein123): Optimally, we would just fire member changed callbacks and remove all of
  // the hosts inside of the HostImpl destructor. That is a change with wide implications, so we
  // are going with a more targeted approach for now.
  drainConnPools();
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::httpConnPoolImpl(
    ResourcePriority priority, absl::optional<Http::Protocol> downstream_protocol,
    LoadBalancerContext* context, bool peek) {
  HostConstSharedPtr host = (peek ? peekAnotherHost(context) : chooseHost(context));
  if (!host) {
    if (!peek) {
      ENVOY_LOG(debug, "no healthy host for HTTP connection pool");
      cluster_info_->trafficStats()->upstream_cx_none_healthy_.inc();
    }
    return nullptr;
  }

  // Right now, HTTP, HTTP/2 and ALPN pools are considered separate.
  // We could do better here, and always use the ALPN pool and simply make sure
  // we end up on a connection of the correct protocol, but for simplicity we're
  // starting with something simpler.
  auto upstream_protocols = host->cluster().upstreamHttpProtocol(downstream_protocol);
  std::vector<uint8_t> hash_key;
  hash_key.reserve(upstream_protocols.size());
  for (auto protocol : upstream_protocols) {
    hash_key.push_back(uint8_t(protocol));
  }

  absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>
      alternate_protocol_options = host->cluster().alternateProtocolsCacheOptions();
  Network::Socket::OptionsSharedPtr upstream_options(std::make_shared<Network::Socket::Options>());
  if (context) {
    // Inherit socket options from downstream connection, if set.
    if (context->downstreamConnection()) {
      addOptionsIfNotNull(upstream_options, context->downstreamConnection()->socketOptions());
    }
    addOptionsIfNotNull(upstream_options, context->upstreamSocketOptions());
  }

  // Use the socket options for computing connection pool hash key, if any.
  // This allows socket options to control connection pooling so that connections with
  // different options are not pooled together.
  for (const auto& option : *upstream_options) {
    option->hashKey(hash_key);
  }

  bool have_transport_socket_options = false;
  if (context && context->upstreamTransportSocketOptions()) {
    host->transportSocketFactory().hashKey(hash_key, context->upstreamTransportSocketOptions());
    have_transport_socket_options = true;
  }

  // If configured, use the downstream connection id in pool hash key
  if (cluster_info_->connectionPoolPerDownstreamConnection() && context &&
      context->downstreamConnection()) {
    context->downstreamConnection()->hashKey(hash_key);
  }

  ConnPoolsContainer& container = *parent_.getHttpConnPoolsContainer(host, true);

  // Note: to simplify this, we assume that the factory is only called in the scope of this
  // function. Otherwise, we'd need to capture a few of these variables by value.
  ConnPoolsContainer::ConnPools::PoolOptRef pool =
      container.pools_->getPool(priority, hash_key, [&]() {
        auto pool = parent_.parent_.factory_.allocateConnPool(
            parent_.thread_local_dispatcher_, host, priority, upstream_protocols,
            alternate_protocol_options, !upstream_options->empty() ? upstream_options : nullptr,
            have_transport_socket_options ? context->upstreamTransportSocketOptions() : nullptr,
            parent_.parent_.time_source_, parent_.cluster_manager_state_, quic_info_);

        pool->addIdleCallback([&parent = parent_, host, priority, hash_key]() {
          parent.httpConnPoolIsIdle(host, priority, hash_key);
        });

        return pool;
      });

  if (pool.has_value()) {
    return &(pool.value().get());
  } else {
    return nullptr;
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::httpConnPoolIsIdle(
    HostConstSharedPtr host, ResourcePriority priority, const std::vector<uint8_t>& hash_key) {
  if (destroying_) {
    // If the Cluster is being destroyed, this pool will be cleaned up by that
    // process.
    return;
  }

  ConnPoolsContainer* container = getHttpConnPoolsContainer(host);
  if (container == nullptr) {
    // This could happen if we have cleaned out the host before iterating through every
    // connection pool. Handle it by just continuing.
    return;
  }

  ENVOY_LOG(trace, "Erasing idle pool for host {}", *host);
  container->pools_->erasePool(priority, hash_key);

  // Guard deletion of the container with `do_not_delete_` to avoid deletion while
  // iterating through the container in `container->pools_->startDrain()`. See
  // comment in `ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools`.
  if (!container->do_not_delete_ && container->pools_->empty()) {
    ENVOY_LOG(trace, "Pool container empty for host {}, erasing host entry", *host);
    host_http_conn_pool_map_.erase(
        host); // NOTE: `container` is erased after this point in the lambda.
  }
}

HostConstSharedPtr ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::chooseHost(
    LoadBalancerContext* context) {
  auto cross_priority_host_map = priority_set_.crossPriorityHostMap();
  HostConstSharedPtr host = HostUtility::selectOverrideHost(cross_priority_host_map.get(),
                                                            override_host_statuses_, context);
  if (host != nullptr) {
    return host;
  }
  if (!HostUtility::allowLBChooseHost(context)) {
    return nullptr;
  }
  return lb_->chooseHost(context);
}

HostConstSharedPtr ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::peekAnotherHost(
    LoadBalancerContext* context) {
  auto cross_priority_host_map = priority_set_.crossPriorityHostMap();
  HostConstSharedPtr host = HostUtility::selectOverrideHost(cross_priority_host_map.get(),
                                                            override_host_statuses_, context);
  if (host != nullptr) {
    return host;
  }
  return lb_->peekAnotherHost(context);
}

Tcp::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConnPoolImpl(
    ResourcePriority priority, LoadBalancerContext* context, bool peek) {

  HostConstSharedPtr host = (peek ? peekAnotherHost(context) : chooseHost(context));
  if (!host) {
    if (!peek) {
      ENVOY_LOG(debug, "no healthy host for TCP connection pool");
      cluster_info_->trafficStats()->upstream_cx_none_healthy_.inc();
    }
    return nullptr;
  }

  // Inherit socket options from downstream connection, if set.
  std::vector<uint8_t> hash_key = {uint8_t(priority)};

  // Use downstream connection socket options for computing connection pool hash key, if any.
  // This allows socket options to control connection pooling so that connections with
  // different options are not pooled together.
  Network::Socket::OptionsSharedPtr upstream_options(std::make_shared<Network::Socket::Options>());
  if (context) {
    if (context->downstreamConnection()) {
      addOptionsIfNotNull(upstream_options, context->downstreamConnection()->socketOptions());
    }
    addOptionsIfNotNull(upstream_options, context->upstreamSocketOptions());
  }

  for (const auto& option : *upstream_options) {
    option->hashKey(hash_key);
  }

  bool have_transport_socket_options = false;
  if (context != nullptr && context->upstreamTransportSocketOptions() != nullptr) {
    have_transport_socket_options = true;
    host->transportSocketFactory().hashKey(hash_key, context->upstreamTransportSocketOptions());
  }

  auto container_iter = parent_.host_tcp_conn_pool_map_.find(host);
  if (container_iter == parent_.host_tcp_conn_pool_map_.end()) {
    container_iter = parent_.host_tcp_conn_pool_map_.try_emplace(host, host->acquireHandle()).first;
  }
  TcpConnPoolsContainer& container = container_iter->second;
  auto pool_iter = container.pools_.find(hash_key);
  if (pool_iter == container.pools_.end()) {
    bool inserted;
    std::tie(pool_iter, inserted) = container.pools_.emplace(
        hash_key,
        parent_.parent_.factory_.allocateTcpConnPool(
            parent_.thread_local_dispatcher_, host, priority,
            !upstream_options->empty() ? upstream_options : nullptr,
            have_transport_socket_options ? context->upstreamTransportSocketOptions() : nullptr,
            parent_.cluster_manager_state_, cluster_info_->tcpPoolIdleTimeout()));
    ASSERT(inserted);
    pool_iter->second->addIdleCallback(
        [&parent = parent_, host, hash_key]() { parent.tcpConnPoolIsIdle(host, hash_key); });
  }

  return pool_iter->second.get();
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::tcpConnPoolIsIdle(
    HostConstSharedPtr host, const std::vector<uint8_t>& hash_key) {
  if (destroying_) {
    // If the Cluster is being destroyed, this pool will be cleaned up by that process.
    return;
  }

  auto it = host_tcp_conn_pool_map_.find(host);
  if (it != host_tcp_conn_pool_map_.end()) {
    TcpConnPoolsContainer& container = it->second;

    auto erase_iter = container.pools_.find(hash_key);
    if (erase_iter != container.pools_.end()) {
      ENVOY_LOG(trace, "Idle pool, erasing pool for host {}", *host);
      thread_local_dispatcher_.deferredDelete(std::move(erase_iter->second));
      container.pools_.erase(erase_iter);
    }

    if (container.pools_.empty()) {
      host_tcp_conn_pool_map_.erase(
          host); // NOTE: `container` is erased after this point in the lambda.
    }
  }
}

ClusterManagerPtr ProdClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  auto cluster_manager_impl = std::unique_ptr<ClusterManagerImpl>{new ClusterManagerImpl(
      bootstrap, *this, context_, stats_, tls_, context_.runtime(), context_.localInfo(),
      context_.accessLogManager(), context_.mainThreadDispatcher(), context_.admin(),
      context_.messageValidationContext(), context_.api(), http_context_, context_.grpcContext(),
      context_.routerContext(), server_)};
  return cluster_manager_impl;
}

Http::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    std::vector<Http::Protocol>& protocols,
    const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
        alternate_protocol_options,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    TimeSource& source, ClusterConnectivityState& state, Http::PersistentQuicInfoPtr& quic_info) {

  Http::HttpServerPropertiesCacheSharedPtr alternate_protocols_cache;
  if (alternate_protocol_options.has_value()) {
    // If there is configuration for an alternate protocols cache, always create one.
    alternate_protocols_cache = alternate_protocols_cache_manager_->getCache(
        alternate_protocol_options.value(), dispatcher);
  } else if (!alternate_protocol_options.has_value() &&
             (protocols.size() == 2 ||
              (protocols.size() == 1 && protocols[0] == Http::Protocol::Http2))) {
    // If there is no configuration for an alternate protocols cache, still
    // create one if there's an HTTP/2 upstream (either explicitly, or for mixed
    // HTTP/1.1 and HTTP/2 pools) to track the max concurrent streams across
    // connections.
    envoy::config::core::v3::AlternateProtocolsCacheOptions default_options;
    default_options.set_name(host->cluster().name());
    alternate_protocols_cache =
        alternate_protocols_cache_manager_->getCache(default_options, dispatcher);
  }

  absl::optional<Http::HttpServerPropertiesCache::Origin> origin =
      getOrigin(transport_socket_options, host);
  if (protocols.size() == 3 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http3", 100) &&
      !transport_socket_options->http11ProxyInfo()) {
    ASSERT(contains(protocols,
                    {Http::Protocol::Http11, Http::Protocol::Http2, Http::Protocol::Http3}));
    ASSERT(alternate_protocol_options.has_value());
    ASSERT(alternate_protocols_cache);
#ifdef ENVOY_ENABLE_QUIC
    Envoy::Http::ConnectivityGrid::ConnectivityOptions coptions{protocols};
    if (quic_info == nullptr) {
      quic_info = Quic::createPersistentQuicInfoForCluster(dispatcher, host->cluster());
    }
    return std::make_unique<Http::ConnectivityGrid>(
        dispatcher, context_.api().randomGenerator(), host, priority, options,
        transport_socket_options, state, source, alternate_protocols_cache, coptions,
        quic_stat_names_, *stats_.rootScope(), *quic_info);
#else
    (void)quic_info;
    // Should be blocked by configuration checking at an earlier point.
    PANIC("unexpected");
#endif
  }
  if (protocols.size() >= 2) {
    if (origin.has_value()) {
      envoy::config::core::v3::AlternateProtocolsCacheOptions default_options;
      default_options.set_name(host->cluster().name());
      alternate_protocols_cache =
          alternate_protocols_cache_manager_->getCache(default_options, dispatcher);
    }

    ASSERT(contains(protocols, {Http::Protocol::Http11, Http::Protocol::Http2}));
    return std::make_unique<Http::HttpConnPoolImplMixed>(
        dispatcher, context_.api().randomGenerator(), host, priority, options,
        transport_socket_options, state, origin, alternate_protocols_cache);
  }
  if (protocols.size() == 1 && protocols[0] == Http::Protocol::Http2 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::Http2::allocateConnPool(dispatcher, context_.api().randomGenerator(), host,
                                         priority, options, transport_socket_options, state, origin,
                                         alternate_protocols_cache);
  }
  if (protocols.size() == 1 && protocols[0] == Http::Protocol::Http3 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http3", 100)) {
#ifdef ENVOY_ENABLE_QUIC
    if (quic_info == nullptr) {
      quic_info = Quic::createPersistentQuicInfoForCluster(dispatcher, host->cluster());
    }
    return Http::Http3::allocateConnPool(dispatcher, context_.api().randomGenerator(), host,
                                         priority, options, transport_socket_options, state,
                                         quic_stat_names_, {}, *stats_.rootScope(), {}, *quic_info);
#else
    UNREFERENCED_PARAMETER(source);
    // Should be blocked by configuration checking at an earlier point.
    PANIC("unexpected");
#endif
  }
  ASSERT(protocols.size() == 1 && protocols[0] == Http::Protocol::Http11);
  return Http::Http1::allocateConnPool(dispatcher, context_.api().randomGenerator(), host, priority,
                                       options, transport_socket_options, state);
}

Tcp::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateTcpConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    ClusterConnectivityState& state,
    absl::optional<std::chrono::milliseconds> tcp_pool_idle_timeout) {
  ENVOY_LOG_MISC(debug, "Allocating TCP conn pool");
  return std::make_unique<Tcp::ConnPoolImpl>(
      dispatcher, host, priority, options, transport_socket_options, state, tcp_pool_idle_timeout);
}

absl::StatusOr<std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr>>
ProdClusterManagerFactory::clusterFromProto(const envoy::config::cluster::v3::Cluster& cluster,
                                            ClusterManager& cm,
                                            Outlier::EventLoggerSharedPtr outlier_event_logger,
                                            bool added_via_api) {
  return ClusterFactoryImplBase::create(cluster, context_, cm, dns_resolver_fn_,
                                        ssl_context_manager_, outlier_event_logger, added_via_api);
}

CdsApiPtr
ProdClusterManagerFactory::createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                                     const xds::core::v3::ResourceLocator* cds_resources_locator,
                                     ClusterManager& cm) {
  // TODO(htuch): Differentiate static vs. dynamic validation visitors.
  return CdsApiImpl::create(cds_config, cds_resources_locator, cm, *stats_.rootScope(),
                            context_.messageValidationContext().dynamicValidationVisitor());
}

} // namespace Upstream
} // namespace Envoy
