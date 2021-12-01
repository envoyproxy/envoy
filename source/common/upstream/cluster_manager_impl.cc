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
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/new_grpc_mux_impl.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_mux/grpc_mux_impl.h"
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
#include "source/common/tcp/original_conn_pool.h"
#include "source/common/upstream/cds_api_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/maglev_lb.h"
#include "source/common/upstream/original_dst_cluster.h"
#include "source/common/upstream/priority_conn_pool_map_impl.h"
#include "source/common/upstream/ring_hash_lb.h"
#include "source/common/upstream/subset_lb.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/http/conn_pool_grid.h"
#include "source/common/http/http3/conn_pool.h"
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

} // namespace

void ClusterManagerInitHelper::addCluster(ClusterManagerCluster& cm_cluster) {
  // See comments in ClusterManagerImpl::addOrUpdateCluster() for why this is only called during
  // server initialization.
  ASSERT(state_ != State::AllClustersInitialized);

  const auto initialize_cb = [&cm_cluster, this] { onClusterInit(cm_cluster); };
  Cluster& cluster = cm_cluster.cluster();
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
      Config::ScopedResume maybe_resume_eds_leds;
      if (cm_.adsMux()) {
        const auto eds_type_url =
            Config::getTypeUrl<envoy::config::endpoint::v3::ClusterLoadAssignment>();
        const auto leds_type_url = Config::getTypeUrl<envoy::config::endpoint::v3::LbEndpoint>();
        maybe_resume_eds_leds = cm_.adsMux()->pause({eds_type_url, leds_type_url});
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
    Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager,
    Event::Dispatcher& main_thread_dispatcher, Server::Admin& admin,
    ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
    Http::Context& http_context, Grpc::Context& grpc_context, Router::Context& router_context)
    : factory_(factory), runtime_(runtime), stats_(stats), tls_(tls),
      random_(api.randomGenerator()),
      bind_config_(bootstrap.cluster_manager().upstream_bind_config()), local_info_(local_info),
      cm_stats_(generateStats(stats)),
      init_helper_(*this, [this](ClusterManagerCluster& cluster) { onClusterInit(cluster); }),
      config_tracker_entry_(
          admin.getConfigTracker().add("clusters",
                                       [this](const Matchers::StringMatcher& name_matcher) {
                                         return dumpClusterConfigs(name_matcher);
                                       })),
      time_source_(main_thread_dispatcher.timeSource()), dispatcher_(main_thread_dispatcher),
      http_context_(http_context), router_context_(router_context),
      cluster_stat_names_(stats.symbolTable()),
      cluster_load_report_stat_names_(stats.symbolTable()),
      cluster_circuit_breakers_stat_names_(stats.symbolTable()),
      cluster_request_response_size_stat_names_(stats.symbolTable()),
      cluster_timeout_budget_stat_names_(stats.symbolTable()),
      subscription_factory_(local_info, main_thread_dispatcher, *this,
                            validation_context.dynamicValidationVisitor(), api) {
  async_client_manager_ = std::make_unique<Grpc::AsyncClientManagerImpl>(
      *this, tls, time_source_, api, grpc_context.statNames());
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

  const auto& dyn_resources = bootstrap.dynamic_resources();

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
            cluster.eds_cluster_config().eds_config().config_source_specifier_case() ==
                envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath);
  };
  // Build book-keeping for which clusters are primary. This is useful when we
  // invoke loadCluster() below and it needs the complete set of primaries.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    if (is_primary_cluster(cluster)) {
      primary_clusters_.insert(cluster.name());
    }
  }
  // Load all the primary clusters.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    if (is_primary_cluster(cluster)) {
      loadCluster(cluster, MessageUtil::hash(cluster), "", false, active_clusters_);
    }
  }

  // Now setup ADS if needed, this might rely on a primary cluster.
  // This is the only point where distinction between delta ADS and state-of-the-world ADS is made.
  // After here, we just have a GrpcMux interface held in ads_mux_, which hides
  // whether the backing implementation is delta or SotW.
  if (dyn_resources.has_ads_config()) {
    if (dyn_resources.ads_config().api_type() ==
        envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
      Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        ads_mux_ = std::make_shared<Config::XdsMux::GrpcMuxDelta>(
            Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_,
                                                           dyn_resources.ads_config(), stats, false)
                ->createUncachedRawAsyncClient(),
            main_thread_dispatcher,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                "envoy.service.discovery.v3.AggregatedDiscoveryService."
                "DeltaAggregatedResources"),
            random_, stats_,
            Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()), local_info,
            dyn_resources.ads_config().set_node_on_first_message_only());
      } else {
        ads_mux_ = std::make_shared<Config::NewGrpcMuxImpl>(
            Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_,
                                                           dyn_resources.ads_config(), stats, false)
                ->createUncachedRawAsyncClient(),
            main_thread_dispatcher,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                "envoy.service.discovery.v3.AggregatedDiscoveryService.DeltaAggregatedResources"),
            random_, stats_,
            Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()), local_info);
      }
    } else {
      Config::Utility::checkTransportVersion(dyn_resources.ads_config());
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        ads_mux_ = std::make_shared<Config::XdsMux::GrpcMuxSotw>(
            Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_,
                                                           dyn_resources.ads_config(), stats, false)
                ->createUncachedRawAsyncClient(),
            main_thread_dispatcher,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                "envoy.service.discovery.v3.AggregatedDiscoveryService."
                "StreamAggregatedResources"),
            random_, stats_,
            Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()), local_info,
            bootstrap.dynamic_resources().ads_config().set_node_on_first_message_only());
      } else {
        ads_mux_ = std::make_shared<Config::GrpcMuxImpl>(
            local_info,
            Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_,
                                                           dyn_resources.ads_config(), stats, false)
                ->createUncachedRawAsyncClient(),
            main_thread_dispatcher,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
                "envoy.service.discovery.v3.AggregatedDiscoveryService.StreamAggregatedResources"),
            random_, stats_,
            Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()),
            bootstrap.dynamic_resources().ads_config().set_node_on_first_message_only());
      }
    }
  } else {
    ads_mux_ = std::make_unique<Config::NullGrpcMuxImpl>();
  }

  // After ADS is initialized, load EDS static clusters as EDS config may potentially need ADS.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // Now load all the secondary clusters.
    if (cluster.type() == envoy::config::cluster::v3::Cluster::EDS &&
        cluster.eds_cluster_config().eds_config().config_source_specifier_case() !=
            envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath) {
      loadCluster(cluster, MessageUtil::hash(cluster), "", false, active_clusters_);
    }
  }

  cm_stats_.cluster_added_.add(bootstrap.static_resources().clusters().size());
  updateClusterCounts();

  absl::optional<ThreadLocalClusterManagerImpl::LocalClusterParams> local_cluster_params;
  if (local_cluster_name_) {
    auto local_cluster = active_clusters_.find(local_cluster_name_.value());
    if (local_cluster == active_clusters_.end()) {
      throw EnvoyException(
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
      cds_resources_locator = std::make_unique<xds::core::v3::ResourceLocator>(
          Config::XdsResourceIdentifier::decodeUrl(dyn_resources.cds_resources_locator()));
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

  ads_mux_->start();
}

void ClusterManagerImpl::initializeSecondaryClusters(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  init_helper_.startInitializingSecondaryClusters();

  const auto& cm_config = bootstrap.cluster_manager();
  if (cm_config.has_load_stats_config()) {
    const auto& load_stats_config = cm_config.load_stats_config();

    Config::Utility::checkTransportVersion(load_stats_config);
    load_stats_reporter_ = std::make_unique<LoadStatsReporter>(
        local_info_, *this, stats_,
        Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_, load_stats_config,
                                                       stats_, false)
            ->createUncachedRawAsyncClient(),
        dispatcher_);
  }
}

ClusterManagerStats ClusterManagerImpl::generateStats(Stats::Scope& scope) {
  const std::string final_prefix = "cluster_manager.";
  return {ALL_CLUSTER_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix))};
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
    cluster_data->second->thread_aware_lb_->initialize();
  }

  // Now setup for cross-thread updates.
  cluster_data->second->member_update_cb_ = cluster.prioritySet().addMemberUpdateCb(
      [&cluster, this](const HostVector&, const HostVector& hosts_removed) -> void {
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
      });

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
  const auto previous_cluster =
      loadCluster(cluster, new_hash, version_info, true, warming_clusters_);
  auto& cluster_entry = warming_clusters_.at(cluster_name);
  if (!all_clusters_initialized) {
    ENVOY_LOG(debug, "add/update cluster {} during init", cluster_name);
    init_helper_.addCluster(*cluster_entry);
  } else {
    ENVOY_LOG(debug, "add/update cluster {} starting warming", cluster_name);
    cluster_entry->cluster_->initialize([this, cluster_name] {
      ENVOY_LOG(debug, "warming cluster {} complete", cluster_name);
      auto state_changed_cluster_entry = warming_clusters_.find(cluster_name);
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
      ASSERT(cluster_manager->thread_local_clusters_.count(cluster_name) == 1);
      ENVOY_LOG(debug, "removing TLS cluster {}", cluster_name);
      for (auto& cb : cluster_manager->update_callbacks_) {
        cb->onClusterRemoval(cluster_name);
      }
      cluster_manager->thread_local_clusters_.erase(cluster_name);
    });
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

ClusterManagerImpl::ClusterDataPtr
ClusterManagerImpl::loadCluster(const envoy::config::cluster::v3::Cluster& cluster,
                                const uint64_t cluster_hash, const std::string& version_info,
                                bool added_via_api, ClusterMap& cluster_map) {
  std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr> new_cluster_pair =
      factory_.clusterFromProto(cluster, *this, outlier_event_logger_, added_via_api);
  auto& new_cluster = new_cluster_pair.first;
  Cluster& cluster_reference = *new_cluster;

  if (!added_via_api) {
    if (cluster_map.find(new_cluster->info()->name()) != cluster_map.end()) {
      throw EnvoyException(
          fmt::format("cluster manager: duplicate cluster '{}'", new_cluster->info()->name()));
    }
  }

  if (cluster_reference.info()->lbType() == LoadBalancerType::ClusterProvided &&
      new_cluster_pair.second == nullptr) {
    throw EnvoyException(fmt::format("cluster manager: cluster provided LB specified but cluster "
                                     "'{}' did not provide one. Check cluster documentation.",
                                     new_cluster->info()->name()));
  }

  if (cluster_reference.info()->lbType() != LoadBalancerType::ClusterProvided &&
      new_cluster_pair.second != nullptr) {
    throw EnvoyException(
        fmt::format("cluster manager: cluster provided LB not specified but cluster "
                    "'{}' provided one. Check cluster documentation.",
                    new_cluster->info()->name()));
  }

  if (new_cluster->healthChecker() != nullptr) {
    new_cluster->healthChecker()->addHostCheckCompleteCb(
        [this](HostSharedPtr host, HealthTransition changed_state) {
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
                        host->address(), host->cluster().name());
        postThreadLocalHealthFailure(host);
      }
    });
  }
  ClusterDataPtr result;
  auto cluster_entry_it = cluster_map.find(cluster_reference.info()->name());
  if (cluster_entry_it != cluster_map.end()) {
    result = std::exchange(cluster_entry_it->second,
                           std::make_unique<ClusterData>(cluster, cluster_hash, version_info,
                                                         added_via_api, std::move(new_cluster),
                                                         time_source_));
  } else {
    bool inserted = false;
    std::tie(cluster_entry_it, inserted) = cluster_map.emplace(
        cluster_reference.info()->name(),
        std::make_unique<ClusterData>(cluster, cluster_hash, version_info, added_via_api,
                                      std::move(new_cluster), time_source_));
    ASSERT(inserted);
  }
  // If an LB is thread aware, create it here. The LB is not initialized until cluster pre-init
  // finishes. For RingHash/Maglev don't create the LB here if subset balancing is enabled,
  // because the thread_aware_lb_ field takes precedence over the subset lb).
  if (cluster_reference.info()->lbType() == LoadBalancerType::RingHash) {
    if (!cluster_reference.info()->lbSubsetInfo().isEnabled()) {
      cluster_entry_it->second->thread_aware_lb_ = std::make_unique<RingHashLoadBalancer>(
          cluster_reference.prioritySet(), cluster_reference.info()->stats(),
          cluster_reference.info()->statsScope(), runtime_, random_,
          cluster_reference.info()->lbRingHashConfig(), cluster_reference.info()->lbConfig());
    }
  } else if (cluster_reference.info()->lbType() == LoadBalancerType::Maglev) {
    if (!cluster_reference.info()->lbSubsetInfo().isEnabled()) {
      cluster_entry_it->second->thread_aware_lb_ = std::make_unique<MaglevLoadBalancer>(
          cluster_reference.prioritySet(), cluster_reference.info()->stats(),
          cluster_reference.info()->statsScope(), runtime_, random_,
          cluster_reference.info()->lbMaglevConfig(), cluster_reference.info()->lbConfig());
    }
  } else if (cluster_reference.info()->lbType() == LoadBalancerType::ClusterProvided) {
    cluster_entry_it->second->thread_aware_lb_ = std::move(new_cluster_pair.second);
  } else if (cluster_reference.info()->lbType() == LoadBalancerType::LoadBalancingPolicyConfig) {
    const auto& policy = cluster_reference.info()->loadBalancingPolicy();
    TypedLoadBalancerFactory* typed_lb_factory = cluster_reference.info()->loadBalancerFactory();
    RELEASE_ASSERT(typed_lb_factory != nullptr, "ClusterInfo should contain a valid factory");
    cluster_entry_it->second->thread_aware_lb_ =
        typed_lb_factory->create(cluster_reference.prioritySet(), cluster_reference.info()->stats(),
                                 cluster_reference.info()->statsScope(), runtime_, random_, policy);
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
    const uint64_t previous_warming = cm_stats_.warming_clusters_.value();
    if (previous_warming == 0 && !warming_clusters_.empty()) {
      resume_cds_ = ads_mux_->pause(type_url);
    } else if (previous_warming > 0 && warming_clusters_.empty()) {
      ASSERT(resume_cds_ != nullptr);
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
    return nullptr;
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

void ClusterManagerImpl::drainConnections(const std::string& cluster) {
  ENVOY_LOG_EVENT(debug, "drain_connections_call", "drainConnections called for cluster {}",
                  cluster);

  tls_.runOnAllThreads([cluster](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    auto cluster_entry = cluster_manager->thread_local_clusters_.find(cluster);
    if (cluster_entry != cluster_manager->thread_local_clusters_.end()) {
      cluster_entry->second->drainAllConnPools();
    }
  });
}

void ClusterManagerImpl::drainConnections() {
  ENVOY_LOG_EVENT(debug, "drain_connections_call_for_all_clusters",
                  "drainConnections called for all clusters");

  tls_.runOnAllThreads([](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    for (const auto& cluster_entry : cluster_manager->thread_local_clusters_) {
      cluster_entry.second->drainAllConnPools();
    }
  });
}

void ClusterManagerImpl::checkActiveStaticCluster(const std::string& cluster) {
  const auto& it = active_clusters_.find(cluster);
  if (it == active_clusters_.end()) {
    throw EnvoyException(fmt::format("Unknown gRPC client cluster '{}'", cluster));
  }
  if (it->second->added_via_api_) {
    throw EnvoyException(fmt::format("gRPC client cluster '{}' is not static", cluster));
  }
}

void ClusterManagerImpl::postThreadLocalRemoveHosts(const Cluster& cluster,
                                                    const HostVector& hosts_removed) {
  tls_.runOnAllThreads([name = cluster.info()->name(),
                        hosts_removed](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    cluster_manager->removeHosts(name, hosts_removed);
  });
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
    per_priority.overprovisioning_factor_ = host_set->overprovisioningFactor();
  }

  HostMapConstSharedPtr host_map = cm_cluster.cluster().prioritySet().crossPriorityHostMap();

  tls_.runOnAllThreads([info = cm_cluster.cluster().info(), params = std::move(params),
                        add_or_update_cluster, load_balancer_factory, map = std::move(host_map)](
                           OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    ThreadLocalClusterManagerImpl::ClusterEntry* new_cluster = nullptr;
    if (add_or_update_cluster) {
      if (cluster_manager->thread_local_clusters_.count(info->name()) > 0) {
        ENVOY_LOG(debug, "updating TLS cluster {}", info->name());
      } else {
        ENVOY_LOG(debug, "adding TLS cluster {}", info->name());
      }

      new_cluster = new ThreadLocalClusterManagerImpl::ClusterEntry(*cluster_manager, info,
                                                                    load_balancer_factory);
      cluster_manager->thread_local_clusters_[info->name()].reset(new_cluster);
    }

    for (const auto& per_priority : params.per_priority_update_params_) {
      cluster_manager->updateClusterMembership(
          info->name(), per_priority.priority_, per_priority.update_hosts_params_,
          per_priority.locality_weights_, per_priority.hosts_added_, per_priority.hosts_removed_,
          per_priority.overprovisioning_factor_, map);
    }

    if (new_cluster != nullptr) {
      for (auto& cb : cluster_manager->update_callbacks_) {
        cb->onClusterAddOrUpdate(*new_cluster);
      }
    }
  });
}

void ClusterManagerImpl::postThreadLocalHealthFailure(const HostSharedPtr& host) {
  tls_.runOnAllThreads([host](OptRef<ThreadLocalClusterManagerImpl> cluster_manager) {
    cluster_manager->onHostHealthFailure(host);
  });
}

Host::CreateConnectionData ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConn(
    LoadBalancerContext* context) {
  HostConstSharedPtr logical_host = lb_->chooseHost(context);
  if (logical_host) {
    auto conn_info = logical_host->createConnection(
        parent_.thread_local_dispatcher_, nullptr,
        context == nullptr ? nullptr : context->upstreamTransportSocketOptions());
    if ((cluster_info_->features() &
         ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) &&
        conn_info.connection_ != nullptr) {
      auto& conn_map = parent_.host_tcp_conn_map_[logical_host];
      conn_map.emplace(conn_info.connection_.get(),
                       std::make_unique<ThreadLocalClusterManagerImpl::TcpConnContainer>(
                           parent_, logical_host, *conn_info.connection_));
    }
    return conn_info;
  } else {
    cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return {nullptr, nullptr};
  }
}

Http::AsyncClient&
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::httpAsyncClient() {
  return http_async_client_;
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::updateHosts(
    const std::string& name, uint32_t priority,
    PrioritySet::UpdateHostsParams&& update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, absl::optional<uint32_t> overprovisioning_factor,
    HostMapConstSharedPtr cross_priority_host_map) {
  ENVOY_LOG(debug, "membership update for TLS cluster {} added {} removed {}", name,
            hosts_added.size(), hosts_removed.size());
  priority_set_.updateHosts(priority, std::move(update_hosts_params), std::move(locality_weights),
                            hosts_added, hosts_removed, overprovisioning_factor,
                            std::move(cross_priority_host_map));
  // If an LB is thread aware, create a new worker local LB on membership changes.
  if (lb_factory_ != nullptr) {
    ENVOY_LOG(debug, "re-creating local LB for TLS cluster {}", name);
    lb_ = lb_factory_->create();
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainConnPools(
    const HostVector& hosts_removed) {
  parent_.drainConnPools(hosts_removed);
}

ClusterUpdateCallbacksHandlePtr
ClusterManagerImpl::addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) {
  ThreadLocalClusterManagerImpl& cluster_manager = *tls_;
  return std::make_unique<ClusterUpdateCallbacksHandleImpl>(cb, cluster_manager.update_callbacks_);
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
    : parent_(parent), thread_local_dispatcher_(dispatcher) {
  // If local cluster is defined then we need to initialize it first.
  if (local_cluster_params.has_value()) {
    const auto& local_cluster_name = local_cluster_params->info_->name();
    ENVOY_LOG(debug, "adding TLS local cluster {}", local_cluster_name);
    thread_local_clusters_[local_cluster_name] = std::make_unique<ClusterEntry>(
        *this, local_cluster_params->info_, local_cluster_params->load_balancer_factory_);
    local_priority_set_ = &thread_local_clusters_[local_cluster_name]->prioritySet();
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

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(const HostVector& hosts) {
  for (const HostSharedPtr& host : hosts) {
    {
      auto container = getHttpConnPoolsContainer(host);
      if (container != nullptr) {
        drainConnPools(host, *container);
      }
    }
    {
      auto container = host_tcp_conn_pool_map_.find(host);
      if (container != host_tcp_conn_pool_map_.end()) {
        drainTcpConnPools(container->second);
      }
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    HostSharedPtr old_host, ConnPoolsContainer& container) {
  // Make a copy to protect against erasure in the callback.
  std::shared_ptr<ConnPoolsContainer::ConnPools> pools = container.pools_;
  container.draining_ = true;

  // We need to hold off on actually emptying out the container until we have finished processing
  // `addIdleCallback`. If we do not, then it's possible that the container could be erased in
  // the middle of its iteration, which leads to undefined behaviour. We handle that case by
  // guarding deletion with `do_not_delete_` in the registered idle callback, and then checking
  // afterwards whether it is empty and deleting it if necessary.
  container.do_not_delete_ = true;
  pools->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  container.do_not_delete_ = false;

  if (container.pools_->empty()) {
    host_http_conn_pool_map_.erase(old_host);
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainTcpConnPools(
    TcpConnPoolsContainer& container) {

  // Copy the pools so that it is safe for the completion callback to mutate `container.pools_`.
  // `container` may be invalid after all calls to `startDrain()`.
  std::vector<Tcp::ConnectionPool::Instance*> pools;
  for (const auto& pair : container.pools_) {
    pools.push_back(pair.second.get());
  }

  container.draining_ = true;
  for (auto pool : pools) {
    pool->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainAndDelete);
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::removeTcpConn(
    const HostConstSharedPtr& host, Network::ClientConnection& connection) {
  auto host_tcp_conn_map_it = host_tcp_conn_map_.find(host);
  ASSERT(host_tcp_conn_map_it != host_tcp_conn_map_.end());
  TcpConnectionsMap& connections_map = host_tcp_conn_map_it->second;
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
  ASSERT(thread_local_clusters_.find(name) != thread_local_clusters_.end());
  const auto& cluster_entry = thread_local_clusters_[name];
  ENVOY_LOG(debug, "removing hosts for TLS cluster {} removed {}", name, hosts_removed.size());

  // We need to go through and purge any connection pools for hosts that got deleted.
  // Even if two hosts actually point to the same address this will be safe, since if a
  // host is readded it will be a different physical HostSharedPtr.
  cluster_entry->drainConnPools(hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, uint32_t priority, PrioritySet::UpdateHostsParams update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, uint64_t overprovisioning_factor,
    HostMapConstSharedPtr cross_priority_host_map) {
  ASSERT(thread_local_clusters_.find(name) != thread_local_clusters_.end());
  const auto& cluster_entry = thread_local_clusters_[name];
  cluster_entry->updateHosts(name, priority, std::move(update_hosts_params),
                             std::move(locality_weights), hosts_added, hosts_removed,
                             overprovisioning_factor, std::move(cross_priority_host_map));
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure(
    const HostSharedPtr& host) {

  // Drain all HTTP and TCP connection pool connections in the case of a host health failure. If
  // outlier/ health is due to `ECMP` flow hashing issues for example, a new set of connections
  // might do better.
  // TODO(mattklein123): This function is currently very specific, but in the future when we do
  // more granular host set changes, we should be able to capture single host changes and make them
  // more targeted.
  drainAllConnPoolsWorker(host);

  if (host->cluster().features() &
      ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) {
    // Close non connection pool TCP connections obtained from tcpConn()
    //
    // TODO(jono): The only remaining user of the non-pooled connections seems to be the statsd
    // TCP client. Perhaps it could be rewritten to use a connection pool, and this code deleted.
    //
    // Each connection will remove itself from the TcpConnectionsMap when it closes, via its
    // Network::ConnectionCallbacks. The last removed tcp conn will remove the TcpConnectionsMap
    // from host_tcp_conn_map_, so do not cache it between iterations.
    //
    // TODO(ggreenway) PERF: If there are a large number of connections, this could take a long time
    // and halt other useful work. Consider breaking up this work. Note that this behavior is noted
    // in the configuration documentation in cluster setting
    // "close_connections_on_host_health_failure". Update the docs if this if this changes.
    while (true) {
      const auto& it = host_tcp_conn_map_.find(host);
      if (it == host_tcp_conn_map_.end()) {
        break;
      }
      TcpConnectionsMap& container = it->second;
      container.begin()->first->close(Network::ConnectionCloseType::NoFlush);
    }
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
    ConnPoolsContainer container{thread_local_dispatcher_, host};
    container_iter = host_http_conn_pool_map_.emplace(host, std::move(container)).first;
  }

  return &container_iter->second;
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
    const LoadBalancerFactorySharedPtr& lb_factory)
    : parent_(parent), lb_factory_(lb_factory), cluster_info_(cluster),
      http_async_client_(cluster, parent.parent_.stats_, parent.thread_local_dispatcher_,
                         parent.parent_.local_info_, parent.parent_, parent.parent_.runtime_,
                         parent.parent_.random_,
                         Router::ShadowWriterPtr{new Router::ShadowWriterImpl(parent.parent_)},
                         parent_.parent_.http_context_, parent_.parent_.router_context_) {
  priority_set_.getOrCreateHostSet(0);

  // TODO(mattklein123): Consider converting other LBs over to thread local. All of them could
  // benefit given the healthy panic, locality, and priority calculations that take place.
  if (cluster->lbSubsetInfo().isEnabled()) {
    lb_ = std::make_unique<SubsetLoadBalancer>(
        cluster->lbType(), priority_set_, parent_.local_priority_set_, cluster->stats(),
        cluster->statsScope(), parent.parent_.runtime_, parent.parent_.random_,
        cluster->lbSubsetInfo(), cluster->lbRingHashConfig(), cluster->lbMaglevConfig(),
        cluster->lbRoundRobinConfig(), cluster->lbLeastRequestConfig(), cluster->lbConfig(),
        parent_.thread_local_dispatcher_.timeSource());
  } else {
    switch (cluster->lbType()) {
    case LoadBalancerType::LeastRequest: {
      ASSERT(lb_factory_ == nullptr);
      lb_ = std::make_unique<LeastRequestLoadBalancer>(
          priority_set_, parent_.local_priority_set_, cluster->stats(), parent.parent_.runtime_,
          parent.parent_.random_, cluster->lbConfig(), cluster->lbLeastRequestConfig(),
          parent.thread_local_dispatcher_.timeSource());
      break;
    }
    case LoadBalancerType::Random: {
      ASSERT(lb_factory_ == nullptr);
      lb_ = std::make_unique<RandomLoadBalancer>(priority_set_, parent_.local_priority_set_,
                                                 cluster->stats(), parent.parent_.runtime_,
                                                 parent.parent_.random_, cluster->lbConfig());
      break;
    }
    case LoadBalancerType::RoundRobin: {
      ASSERT(lb_factory_ == nullptr);
      lb_ = std::make_unique<RoundRobinLoadBalancer>(
          priority_set_, parent_.local_priority_set_, cluster->stats(), parent.parent_.runtime_,
          parent.parent_.random_, cluster->lbConfig(), cluster->lbRoundRobinConfig(),
          parent.thread_local_dispatcher_.timeSource());
      break;
    }
    case LoadBalancerType::ClusterProvided:
    case LoadBalancerType::LoadBalancingPolicyConfig:
    case LoadBalancerType::RingHash:
    case LoadBalancerType::Maglev:
    case LoadBalancerType::OriginalDst: {
      ASSERT(lb_factory_ != nullptr);
      lb_ = lb_factory_->create();
      break;
    }
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainConnPools() {
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    parent_.drainConnPools(host_set->hosts());
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::drainAllConnPools() {
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    for (const HostSharedPtr& host : host_set->hosts()) {
      parent_.drainAllConnPoolsWorker(host);
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainAllConnPoolsWorker(
    const HostSharedPtr& host) {
  // Drain or close any HTTP connection pool for the host. Draining an HTTP pool only leads to
  // idle connections being closed. Non-idle connections are marked as draining and prevents new
  // streams to go through them, causing new connections to be opened.
  {
    const auto container = getHttpConnPoolsContainer(host);
    if (container != nullptr) {
      container->do_not_delete_ = true;
      container->pools_->drainConnections(
          Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
      container->do_not_delete_ = false;

      if (container->pools_->empty()) {
        host_http_conn_pool_map_.erase(host);
      }
    }
  }
  {
    // Drain or close any TCP connection pool for the host. Draining a TCP pool doesn't lead to
    // connections being closed, it only prevents new connections through the pool. The
    // CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE can be used to make the pool close any
    // active connections.
    const auto& container = host_tcp_conn_pool_map_.find(host);
    if (container != host_tcp_conn_pool_map_.end()) {
      // Draining pools or closing connections can cause pool deletion if it becomes
      // idle. Copy `pools_` so that we aren't iterating through a container that
      // gets mutated by callbacks deleting from it.
      std::vector<Tcp::ConnectionPool::Instance*> pools;
      for (const auto& pair : container->second.pools_) {
        pools.push_back(pair.second.get());
      }

      for (auto* pool : pools) {
        if (host->cluster().features() &
            ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) {
          pool->closeConnections();
        } else {
          pool->drainConnections(Envoy::ConnectionPool::DrainBehavior::DrainExistingConnections);
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
  // the hosts inside of the HostImpl destructor. That is a change with wide implications, so we are
  // going with a more targeted approach for now.
  drainConnPools();
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::httpConnPoolImpl(
    ResourcePriority priority, absl::optional<Http::Protocol> downstream_protocol,
    LoadBalancerContext* context, bool peek) {
  HostConstSharedPtr host = (peek ? lb_->peekAnotherHost(context) : lb_->chooseHost(context));
  if (!host) {
    if (!peek) {
      ENVOY_LOG(debug, "no healthy host for HTTP connection pool");
      cluster_info_->stats().upstream_cx_none_healthy_.inc();
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
    context->upstreamTransportSocketOptions()->hashKey(hash_key, host->transportSocketFactory());
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
            parent_.parent_.time_source_, parent_.cluster_manager_state_);

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

  if (container->draining_ ||
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.conn_pool_delete_when_idle")) {

    ENVOY_LOG(trace, "Erasing idle pool for host {}", host);
    container->pools_->erasePool(priority, hash_key);

    // Guard deletion of the container with `do_not_delete_` to avoid deletion while
    // iterating through the container in `container->pools_->startDrain()`. See
    // comment in `ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools`.
    if (!container->do_not_delete_ && container->pools_->empty()) {
      ENVOY_LOG(trace, "Pool container empty for host {}, erasing host entry", host);
      host_http_conn_pool_map_.erase(
          host); // NOTE: `container` is erased after this point in the lambda.
    }
  }
}

Tcp::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConnPoolImpl(
    ResourcePriority priority, LoadBalancerContext* context, bool peek) {
  HostConstSharedPtr host = (peek ? lb_->peekAnotherHost(context) : lb_->chooseHost(context));
  if (!host) {
    if (!peek) {
      ENVOY_LOG(debug, "no healthy host for TCP connection pool");
      cluster_info_->stats().upstream_cx_none_healthy_.inc();
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
    context->upstreamTransportSocketOptions()->hashKey(hash_key, host->transportSocketFactory());
  }

  TcpConnPoolsContainer& container = parent_.host_tcp_conn_pool_map_[host];
  auto pool_iter = container.pools_.find(hash_key);
  if (pool_iter == container.pools_.end()) {
    bool inserted;
    std::tie(pool_iter, inserted) = container.pools_.emplace(
        hash_key,
        parent_.parent_.factory_.allocateTcpConnPool(
            parent_.thread_local_dispatcher_, host, priority,
            !upstream_options->empty() ? upstream_options : nullptr,
            have_transport_socket_options ? context->upstreamTransportSocketOptions() : nullptr,
            parent_.cluster_manager_state_));
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
      if (container.draining_ ||
          Runtime::runtimeFeatureEnabled("envoy.reloadable_features.conn_pool_delete_when_idle")) {
        ENVOY_LOG(trace, "Idle pool, erasing pool for host {}", host);
        thread_local_dispatcher_.deferredDelete(std::move(erase_iter->second));
        container.pools_.erase(erase_iter);
      }
    }

    if (container.pools_.empty()) {
      host_tcp_conn_pool_map_.erase(
          host); // NOTE: `container` is erased after this point in the lambda.
    }
  }
}

ClusterManagerPtr ProdClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  return ClusterManagerPtr{new ClusterManagerImpl(
      bootstrap, *this, stats_, tls_, context_.runtime(), context_.localInfo(), log_manager_,
      context_.mainThreadDispatcher(), context_.admin(), validation_context_, context_.api(),
      http_context_, grpc_context_, router_context_)};
}

Http::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    std::vector<Http::Protocol>& protocols,
    const absl::optional<envoy::config::core::v3::AlternateProtocolsCacheOptions>&
        alternate_protocol_options,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    TimeSource& source, ClusterConnectivityState& state) {
  if (protocols.size() == 3 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http3", 100)) {
    ASSERT(contains(protocols,
                    {Http::Protocol::Http11, Http::Protocol::Http2, Http::Protocol::Http3}));
    ASSERT(alternate_protocol_options.has_value());
#ifdef ENVOY_ENABLE_QUIC
    Http::AlternateProtocolsCacheSharedPtr alternate_protocols_cache =
        alternate_protocols_cache_manager_->getCache(alternate_protocol_options.value(),
                                                     dispatcher);
    Envoy::Http::ConnectivityGrid::ConnectivityOptions coptions{protocols};
    return std::make_unique<Http::ConnectivityGrid>(
        dispatcher, context_.api().randomGenerator(), host, priority, options,
        transport_socket_options, state, source, alternate_protocols_cache,
        std::chrono::milliseconds(300), coptions, quic_stat_names_, stats_);
#else
    // Should be blocked by configuration checking at an earlier point.
    NOT_REACHED_GCOVR_EXCL_LINE;
#endif
  }
  if (protocols.size() >= 2) {
    ASSERT(contains(protocols, {Http::Protocol::Http11, Http::Protocol::Http2}));
    return std::make_unique<Http::HttpConnPoolImplMixed>(
        dispatcher, context_.api().randomGenerator(), host, priority, options,
        transport_socket_options, state);
  }
  if (protocols.size() == 1 && protocols[0] == Http::Protocol::Http2 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::Http2::allocateConnPool(dispatcher, context_.api().randomGenerator(), host,
                                         priority, options, transport_socket_options, state);
  }
  if (protocols.size() == 1 && protocols[0] == Http::Protocol::Http3 &&
      context_.runtime().snapshot().featureEnabled("upstream.use_http3", 100)) {
#ifdef ENVOY_ENABLE_QUIC
    return Http::Http3::allocateConnPool(dispatcher, context_.api().randomGenerator(), host,
                                         priority, options, transport_socket_options, state, source,
                                         quic_stat_names_, stats_);
#else
    UNREFERENCED_PARAMETER(source);
    // Should be blocked by configuration checking at an earlier point.
    NOT_REACHED_GCOVR_EXCL_LINE;
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
    ClusterConnectivityState& state) {
  ENVOY_LOG_MISC(debug, "Allocating TCP conn pool");
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.new_tcp_connection_pool")) {
    return std::make_unique<Tcp::ConnPoolImpl>(dispatcher, host, priority, options,
                                               transport_socket_options, state);
  } else {
    return Tcp::ConnectionPool::InstancePtr{new Tcp::OriginalConnPoolImpl(
        dispatcher, host, priority, options, transport_socket_options)};
  }
}

std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr> ProdClusterManagerFactory::clusterFromProto(
    const envoy::config::cluster::v3::Cluster& cluster, ClusterManager& cm,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) {
  return ClusterFactoryImplBase::create(
      cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_, context_.runtime(),
      context_.mainThreadDispatcher(), log_manager_, context_.localInfo(), admin_,
      singleton_manager_, outlier_event_logger, added_via_api,
      added_via_api ? validation_context_.dynamicValidationVisitor()
                    : validation_context_.staticValidationVisitor(),
      context_.api(), context_.options());
}

CdsApiPtr
ProdClusterManagerFactory::createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                                     const xds::core::v3::ResourceLocator* cds_resources_locator,
                                     ClusterManager& cm) {
  // TODO(htuch): Differentiate static vs. dynamic validation visitors.
  return CdsApiImpl::create(cds_config, cds_resources_locator, cm, stats_,
                            validation_context_.dynamicValidationVisitor());
}

} // namespace Upstream
} // namespace Envoy
