#include "common/upstream/cluster_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/new_grpc_mux_impl.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/grpc/async_client_manager_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/json/config_schemas.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/shadow_writer_impl.h"
#include "common/tcp/conn_pool.h"
#include "common/upstream/cds_api_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/maglev_lb.h"
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/priority_conn_pool_map_impl.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/subset_lb.h"

namespace Envoy {
namespace Upstream {
namespace {

void addOptionsIfNotNull(Network::Socket::OptionsSharedPtr& options,
                         const Network::Socket::OptionsSharedPtr& to_add) {
  if (to_add != nullptr) {
    Network::Socket::appendOptions(options, to_add);
  }
}

} // namespace

void ClusterManagerInitHelper::addCluster(Cluster& cluster) {
  // See comments in ClusterManagerImpl::addOrUpdateCluster() for why this is only called during
  // server initialization.
  ASSERT(state_ != State::AllClustersInitialized);

  const auto initialize_cb = [&cluster, this] { onClusterInit(cluster); };
  if (cluster.initializePhase() == Cluster::InitializePhase::Primary) {
    primary_init_clusters_.push_back(&cluster);
    cluster.initialize(initialize_cb);
  } else {
    ASSERT(cluster.initializePhase() == Cluster::InitializePhase::Secondary);
    secondary_init_clusters_.push_back(&cluster);
    if (started_secondary_initialize_) {
      // This can happen if we get a second CDS update that adds new clusters after we have
      // already started secondary init. In this case, just immediately initialize.
      cluster.initialize(initialize_cb);
    }
  }

  ENVOY_LOG(debug, "cm init: adding: cluster={} primary={} secondary={}", cluster.info()->name(),
            primary_init_clusters_.size(), secondary_init_clusters_.size());
}

void ClusterManagerInitHelper::onClusterInit(Cluster& cluster) {
  ASSERT(state_ != State::AllClustersInitialized);
  per_cluster_init_callback_(cluster);
  removeCluster(cluster);
}

void ClusterManagerInitHelper::removeCluster(Cluster& cluster) {
  if (state_ == State::AllClustersInitialized) {
    return;
  }

  // There is a remote edge case where we can remove a cluster via CDS that has not yet been
  // initialized. When called via the remove cluster API this code catches that case.
  std::list<Cluster*>* cluster_list;
  if (cluster.initializePhase() == Cluster::InitializePhase::Primary) {
    cluster_list = &primary_init_clusters_;
  } else {
    ASSERT(cluster.initializePhase() == Cluster::InitializePhase::Secondary);
    cluster_list = &secondary_init_clusters_;
  }

  // It is possible that the cluster we are removing has already been initialized, and is not
  // present in the initializer list. If so, this is fine.
  cluster_list->remove(&cluster);
  ENVOY_LOG(debug, "cm init: init complete: cluster={} primary={} secondary={}",
            cluster.info()->name(), primary_init_clusters_.size(), secondary_init_clusters_.size());
  maybeFinishInitialize();
}

void ClusterManagerInitHelper::initializeSecondaryClusters() {
  started_secondary_initialize_ = true;
  // Cluster::initialize() method can modify the list of secondary_init_clusters_ to remove
  // the item currently being initialized, so we eschew range-based-for and do this complicated
  // dance to increment the iterator before calling initialize.
  for (auto iter = secondary_init_clusters_.begin(); iter != secondary_init_clusters_.end();) {
    Cluster* cluster = *iter;
    ++iter;
    ENVOY_LOG(debug, "initializing secondary cluster {}", cluster->info()->name());
    cluster->initialize([cluster, this] { onClusterInit(*cluster); });
  }
}

void ClusterManagerInitHelper::maybeFinishInitialize() {
  // Do not do anything if we are still doing the initial static load or if we are waiting for
  // CDS initialize.
  if (state_ == State::Loading || state_ == State::WaitingForCdsInitialize) {
    return;
  }

  // If we are still waiting for primary clusters to initialize, do nothing.
  ASSERT(state_ == State::WaitingForStaticInitialize || state_ == State::CdsInitialized);
  if (!primary_init_clusters_.empty()) {
    return;
  }

  // If we are still waiting for secondary clusters to initialize, see if we need to first call
  // initialize on them. This is only done once.
  if (!secondary_init_clusters_.empty()) {
    if (!started_secondary_initialize_) {
      ENVOY_LOG(info, "cm init: initializing secondary clusters");
      // If the first CDS response doesn't have any primary cluster, ClusterLoadAssignment
      // should be already paused by CdsApiImpl::onConfigUpdate(). Need to check that to
      // avoid double pause ClusterLoadAssignment.
      if (cm_.adsMux() == nullptr ||
          cm_.adsMux()->paused(Config::TypeUrl::get().ClusterLoadAssignment)) {
        initializeSecondaryClusters();
      } else {
        cm_.adsMux()->pause(Config::TypeUrl::get().ClusterLoadAssignment);
        Cleanup eds_resume(
            [this] { cm_.adsMux()->resume(Config::TypeUrl::get().ClusterLoadAssignment); });
        initializeSecondaryClusters();
      }
    }
    return;
  }

  // At this point, if we are doing static init, and we have CDS, start CDS init. Otherwise, move
  // directly to initialized.
  started_secondary_initialize_ = false;
  if (state_ == State::WaitingForStaticInitialize && cds_) {
    ENVOY_LOG(info, "cm init: initializing cds");
    state_ = State::WaitingForCdsInitialize;
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
  state_ = State::WaitingForStaticInitialize;
  maybeFinishInitialize();
}

void ClusterManagerInitHelper::setCds(CdsApi* cds) {
  ASSERT(state_ == State::Loading);
  cds_ = cds;
  if (cds_) {
    cds_->setInitializedCb([this]() -> void {
      ASSERT(state_ == State::WaitingForCdsInitialize);
      state_ = State::CdsInitialized;
      maybeFinishInitialize();
    });
  }
}

void ClusterManagerInitHelper::setInitializedCb(std::function<void()> callback) {
  if (state_ == State::AllClustersInitialized) {
    callback();
  } else {
    initialized_callback_ = callback;
  }
}

ClusterManagerImpl::ClusterManagerImpl(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap, ClusterManagerFactory& factory,
    Stats::Store& stats, ThreadLocal::Instance& tls, Runtime::Loader& runtime,
    Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
    AccessLog::AccessLogManager& log_manager, Event::Dispatcher& main_thread_dispatcher,
    Server::Admin& admin, ProtobufMessage::ValidationContext& validation_context, Api::Api& api,
    Http::Context& http_context)
    : factory_(factory), runtime_(runtime), stats_(stats), tls_(tls.allocateSlot()),
      random_(random), bind_config_(bootstrap.cluster_manager().upstream_bind_config()),
      local_info_(local_info), cm_stats_(generateStats(stats)),
      init_helper_(*this, [this](Cluster& cluster) { onClusterInit(cluster); }),
      config_tracker_entry_(
          admin.getConfigTracker().add("clusters", [this] { return dumpClusterConfigs(); })),
      time_source_(main_thread_dispatcher.timeSource()), dispatcher_(main_thread_dispatcher),
      http_context_(http_context),
      subscription_factory_(local_info, main_thread_dispatcher, *this, random,
                            validation_context.dynamicValidationVisitor(), api) {
  async_client_manager_ =
      std::make_unique<Grpc::AsyncClientManagerImpl>(*this, tls, time_source_, api);
  const auto& cm_config = bootstrap.cluster_manager();
  if (cm_config.has_outlier_detection()) {
    const std::string event_log_file_path = cm_config.outlier_detection().event_log_path();
    if (!event_log_file_path.empty()) {
      outlier_event_logger_.reset(
          new Outlier::EventLoggerImpl(log_manager, event_log_file_path, time_source_));
    }
  }

  const auto& dyn_resources = bootstrap.dynamic_resources();

  // Cluster loading happens in two phases: first all the primary clusters are loaded, and then all
  // the secondary clusters are loaded. As it currently stands all non-EDS clusters are primary and
  // only EDS clusters are secondary. This two phase loading is done because in v2 configuration
  // each EDS cluster individually sets up a subscription. When this subscription is an API source
  // the cluster will depend on a non-EDS cluster, so the non-EDS clusters must be loaded first.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // First load all the primary clusters.
    if (cluster.type() != envoy::api::v2::Cluster::EDS) {
      loadCluster(cluster, "", false, active_clusters_);
    }
  }

  // Now setup ADS if needed, this might rely on a primary cluster.
  // This is the only point where distinction between delta ADS and state-of-the-world ADS is made.
  // After here, we just have a GrpcMux interface held in ads_mux_, which hides
  // whether the backing implementation is delta or SotW.
  if (dyn_resources.has_ads_config()) {
    if (dyn_resources.ads_config().api_type() ==
        envoy::api::v2::core::ApiConfigSource::DELTA_GRPC) {
      auto& api_config_source = dyn_resources.has_ads_config()
                                    ? dyn_resources.ads_config()
                                    : dyn_resources.cds_config().api_config_source();
      ads_mux_ = std::make_shared<Config::NewGrpcMuxImpl>(
          Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_, api_config_source,
                                                         stats)
              ->create(),
          main_thread_dispatcher,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.DeltaAggregatedResources"),
          random_, stats_,
          Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()), local_info);
    } else {
      ads_mux_ = std::make_shared<Config::GrpcMuxImpl>(
          local_info,
          Config::Utility::factoryForGrpcApiConfigSource(*async_client_manager_,
                                                         dyn_resources.ads_config(), stats)
              ->create(),
          main_thread_dispatcher,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
              "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources"),
          random_, stats_,
          Envoy::Config::Utility::parseRateLimitSettings(dyn_resources.ads_config()),
          bootstrap.dynamic_resources().ads_config().set_node_on_first_message_only());
    }
  } else {
    ads_mux_ = std::make_unique<Config::NullGrpcMuxImpl>();
  }

  // After ADS is initialized, load EDS static clusters as EDS config may potentially need ADS.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // Now load all the secondary clusters.
    if (cluster.type() == envoy::api::v2::Cluster::EDS) {
      loadCluster(cluster, "", false, active_clusters_);
    }
  }

  cm_stats_.cluster_added_.add(bootstrap.static_resources().clusters().size());
  updateClusterCounts();

  absl::optional<std::string> local_cluster_name;
  if (!cm_config.local_cluster_name().empty()) {
    local_cluster_name_ = cm_config.local_cluster_name();
    local_cluster_name = cm_config.local_cluster_name();
    if (active_clusters_.find(local_cluster_name.value()) == active_clusters_.end()) {
      throw EnvoyException(
          fmt::format("local cluster '{}' must be defined", local_cluster_name.value()));
    }
  }

  // Once the initial set of static bootstrap clusters are created (including the local cluster),
  // we can instantiate the thread local cluster manager.
  tls_->set([this, local_cluster_name](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalClusterManagerImpl>(*this, dispatcher, local_cluster_name);
  });

  // We can now potentially create the CDS API once the backing cluster exists.
  if (dyn_resources.has_cds_config()) {
    cds_api_ = factory_.createCds(dyn_resources.cds_config(), *this);
    init_helper_.setCds(cds_api_.get());
  } else {
    init_helper_.setCds(nullptr);
  }

  // Proceed to add all static bootstrap clusters to the init manager. This will immediately
  // initialize any primary clusters. Post-init processing further initializes any thread
  // aware load balancer and sets up the per-worker host set updates.
  for (auto& cluster : active_clusters_) {
    init_helper_.addCluster(*cluster.second->cluster_);
  }

  // Potentially move to secondary initialization on the static bootstrap clusters if all primary
  // clusters have already initialized. (E.g., if all static).
  init_helper_.onStaticLoadComplete();

  ads_mux_->start();

  if (cm_config.has_load_stats_config()) {
    const auto& load_stats_config = cm_config.load_stats_config();
    load_stats_reporter_ =
        std::make_unique<LoadStatsReporter>(local_info, *this, stats,
                                            Config::Utility::factoryForGrpcApiConfigSource(
                                                *async_client_manager_, load_stats_config, stats)
                                                ->create(),
                                            main_thread_dispatcher);
  }
}

ClusterManagerStats ClusterManagerImpl::generateStats(Stats::Scope& scope) {
  const std::string final_prefix = "cluster_manager.";
  return {ALL_CLUSTER_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void ClusterManagerImpl::onClusterInit(Cluster& cluster) {
  // This routine is called when a cluster has finished initializing. The cluster has not yet
  // been setup for cross-thread updates to avoid needless updates during initialization. The order
  // of operations here is important. We start by initializing the thread aware load balancer if
  // needed. This must happen first so cluster updates are heard first by the load balancer.
  auto cluster_data = active_clusters_.find(cluster.info()->name());
  if (cluster_data->second->thread_aware_lb_ != nullptr) {
    cluster_data->second->thread_aware_lb_->initialize();
  }

  // Now setup for cross-thread updates.
  cluster.prioritySet().addMemberUpdateCb(
      [&cluster, this](const HostVector&, const HostVector& hosts_removed) -> void {
        if (cluster.info()->lbConfig().close_connections_on_host_set_change()) {
          for (const auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
            // This will drain all tcp and http connection pools.
            postThreadLocalDrainConnections(cluster, host_set->hosts());
          }
        } else {
          // TODO(snowp): Should this be subject to merge windows?

          // Whenever hosts are removed from the cluster, we make each TLS cluster drain it's
          // connection pools for the removed hosts. If `close_connections_on_host_set_change` is
          // enabled, this case will be covered by first `if` statement, where all
          // connection pools are drained.
          if (!hosts_removed.empty()) {
            postThreadLocalDrainConnections(cluster, hosts_removed);
          }
        }
      });

  cluster.prioritySet().addPriorityUpdateCb([&cluster, this](uint32_t priority,
                                                             const HostVector& hosts_added,
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
    // HostSharedPtrs within internal maps to track hosts. If we fail to broadcast the entire list
    // of removals, these maps will leak those HostSharedPtrs.
    //
    // See https://github.com/envoyproxy/envoy/pull/3941 for more context.
    bool scheduled = false;
    const auto merge_timeout =
        PROTOBUF_GET_MS_OR_DEFAULT(cluster.info()->lbConfig(), update_merge_window, 1000);
    // Remember: we only merge updates with no adds/removes — just hc/weight/metadata changes.
    const bool is_mergeable = hosts_added.empty() && hosts_removed.empty();

    if (merge_timeout > 0) {
      // If this is not mergeable, we should cancel any scheduled updates since
      // we'll deliver it immediately.
      scheduled = scheduleUpdate(cluster, priority, is_mergeable, merge_timeout);
    }

    // If an update was not scheduled for later, deliver it immediately.
    if (!scheduled) {
      cm_stats_.cluster_updated_.inc();
      postThreadLocalClusterUpdate(cluster, priority, hosts_added, hosts_removed);
    }
  });

  // Finally, if the cluster has any hosts, post updates cross-thread so the per-thread load
  // balancers are ready.
  for (auto& host_set : cluster.prioritySet().hostSetsPerPriority()) {
    if (host_set->hosts().empty()) {
      continue;
    }
    postThreadLocalClusterUpdate(cluster, host_set->priority(), host_set->hosts(), HostVector{});
  }
}

bool ClusterManagerImpl::scheduleUpdate(const Cluster& cluster, uint32_t priority, bool mergeable,
                                        const uint64_t timeout) {
  // Find pending updates for this cluster.
  auto& updates_by_prio = updates_map_[cluster.info()->name()];
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
  if (!updates->timer_enabled_) {
    updates->enableTimer(timeout);
  }

  return true;
}

void ClusterManagerImpl::applyUpdates(const Cluster& cluster, uint32_t priority,
                                      PendingUpdates& updates) {
  // Deliver pending updates.

  // Remember that these merged updates are _only_ for updates related to
  // HC/weight/metadata changes. That's why added/removed are empty. All
  // adds/removals were already immediately broadcasted.
  static const HostVector hosts_added;
  static const HostVector hosts_removed;

  postThreadLocalClusterUpdate(cluster, priority, hosts_added, hosts_removed);

  cm_stats_.cluster_updated_via_merge_.inc();
  updates.timer_enabled_ = false;
  updates.last_updated_ = time_source_.monotonicTime();
}

bool ClusterManagerImpl::addOrUpdateCluster(const envoy::api::v2::Cluster& cluster,
                                            const std::string& version_info) {
  // First we need to see if this new config is new or an update to an existing dynamic cluster.
  // We don't allow updates to statically configured clusters in the main configuration. We check
  // both the warming clusters and the active clusters to see if we need an update or the update
  // should be blocked.
  const std::string& cluster_name = cluster.name();
  const auto existing_active_cluster = active_clusters_.find(cluster_name);
  const auto existing_warming_cluster = warming_clusters_.find(cluster_name);
  const uint64_t new_hash = MessageUtil::hash(cluster);
  if ((existing_active_cluster != active_clusters_.end() &&
       existing_active_cluster->second->blockUpdate(new_hash)) ||
      (existing_warming_cluster != warming_clusters_.end() &&
       existing_warming_cluster->second->blockUpdate(new_hash))) {
    return false;
  }

  if (existing_active_cluster != active_clusters_.end() ||
      existing_warming_cluster != warming_clusters_.end()) {
    if (existing_active_cluster != active_clusters_.end()) {
      // The following init manager remove call is a NOP in the case we are already initialized.
      // It's just kept here to avoid additional logic.
      init_helper_.removeCluster(*existing_active_cluster->second->cluster_);
    } else {
      // Validate that warming clusters are not added to the init_helper_.
      // NOTE: This loop is compiled out in optimized builds.
      for (const std::list<Cluster*>& cluster_list :
           {std::cref(init_helper_.primary_init_clusters_),
            std::cref(init_helper_.secondary_init_clusters_)}) {
        ASSERT(!std::any_of(cluster_list.begin(), cluster_list.end(),
                            [&existing_warming_cluster](Cluster* cluster) {
                              return existing_warming_cluster->second->cluster_.get() == cluster;
                            }));
      }
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
  const bool use_active_map =
      init_helper_.state() != ClusterManagerInitHelper::State::AllClustersInitialized;
  loadCluster(cluster, version_info, true, use_active_map ? active_clusters_ : warming_clusters_);

  if (use_active_map) {
    ENVOY_LOG(debug, "add/update cluster {} during init", cluster_name);
    auto& cluster_entry = active_clusters_.at(cluster_name);
    createOrUpdateThreadLocalCluster(*cluster_entry);
    init_helper_.addCluster(*cluster_entry->cluster_);
  } else {
    auto& cluster_entry = warming_clusters_.at(cluster_name);
    ENVOY_LOG(debug, "add/update cluster {} starting warming", cluster_name);
    cluster_entry->cluster_->initialize([this, cluster_name] {
      auto warming_it = warming_clusters_.find(cluster_name);
      auto& cluster_entry = *warming_it->second;

      // If the cluster is being updated, we need to cancel any pending merged updates.
      // Otherwise, applyUpdates() will fire with a dangling cluster reference.
      updates_map_.erase(cluster_name);

      active_clusters_[cluster_name] = std::move(warming_it->second);
      warming_clusters_.erase(warming_it);

      ENVOY_LOG(debug, "warming cluster {} complete", cluster_name);
      createOrUpdateThreadLocalCluster(cluster_entry);
      onClusterInit(*cluster_entry.cluster_);
      updateClusterCounts();
    });
  }

  updateClusterCounts();
  return true;
}

void ClusterManagerImpl::createOrUpdateThreadLocalCluster(ClusterData& cluster) {
  tls_->runOnAllThreads([this, new_cluster = cluster.cluster_->info(),
                         thread_aware_lb_factory = cluster.loadBalancerFactory()]() -> void {
    ThreadLocalClusterManagerImpl& cluster_manager =
        tls_->getTyped<ThreadLocalClusterManagerImpl>();

    if (cluster_manager.thread_local_clusters_.count(new_cluster->name()) > 0) {
      ENVOY_LOG(debug, "updating TLS cluster {}", new_cluster->name());
    } else {
      ENVOY_LOG(debug, "adding TLS cluster {}", new_cluster->name());
    }

    auto thread_local_cluster = new ThreadLocalClusterManagerImpl::ClusterEntry(
        cluster_manager, new_cluster, thread_aware_lb_factory);
    cluster_manager.thread_local_clusters_[new_cluster->name()].reset(thread_local_cluster);
    for (auto& cb : cluster_manager.update_callbacks_) {
      cb->onClusterAddOrUpdate(*thread_local_cluster);
    }
  });
}

bool ClusterManagerImpl::removeCluster(const std::string& cluster_name) {
  bool removed = false;
  auto existing_active_cluster = active_clusters_.find(cluster_name);
  if (existing_active_cluster != active_clusters_.end() &&
      existing_active_cluster->second->added_via_api_) {
    removed = true;
    init_helper_.removeCluster(*existing_active_cluster->second->cluster_);
    active_clusters_.erase(existing_active_cluster);

    ENVOY_LOG(info, "removing cluster {}", cluster_name);
    tls_->runOnAllThreads([this, cluster_name]() -> void {
      ThreadLocalClusterManagerImpl& cluster_manager =
          tls_->getTyped<ThreadLocalClusterManagerImpl>();

      ASSERT(cluster_manager.thread_local_clusters_.count(cluster_name) == 1);
      ENVOY_LOG(debug, "removing TLS cluster {}", cluster_name);
      for (auto& cb : cluster_manager.update_callbacks_) {
        cb->onClusterRemoval(cluster_name);
      }
      cluster_manager.thread_local_clusters_.erase(cluster_name);
    });
  }

  auto existing_warming_cluster = warming_clusters_.find(cluster_name);
  if (existing_warming_cluster != warming_clusters_.end() &&
      existing_warming_cluster->second->added_via_api_) {
    removed = true;
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

void ClusterManagerImpl::loadCluster(const envoy::api::v2::Cluster& cluster,
                                     const std::string& version_info, bool added_via_api,
                                     ClusterMap& cluster_map) {
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
        postThreadLocalHealthFailure(host);
      }
    });
  }

  cluster_map[cluster_reference.info()->name()] = std::make_unique<ClusterData>(
      cluster, version_info, added_via_api, std::move(new_cluster), time_source_);
  const auto cluster_entry_it = cluster_map.find(cluster_reference.info()->name());

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
          cluster_reference.info()->lbConfig());
    }
  } else if (cluster_reference.info()->lbType() == LoadBalancerType::ClusterProvided) {
    cluster_entry_it->second->thread_aware_lb_ = std::move(new_cluster_pair.second);
  }

  updateClusterCounts();
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
  if (ads_mux_) {
    const uint64_t previous_warming = cm_stats_.warming_clusters_.value();
    if (previous_warming == 0 && !warming_clusters_.empty()) {
      ads_mux_->pause(Config::TypeUrl::get().Cluster);
    } else if (previous_warming > 0 && warming_clusters_.empty()) {
      ads_mux_->resume(Config::TypeUrl::get().Cluster);
    }
  }
  cm_stats_.active_clusters_.set(active_clusters_.size());
  cm_stats_.warming_clusters_.set(warming_clusters_.size());
}

ThreadLocalCluster* ClusterManagerImpl::get(absl::string_view cluster) {
  auto& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry != cluster_manager.thread_local_clusters_.end()) {
    return entry->second.get();
  } else {
    return nullptr;
  }
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::httpConnPoolForCluster(const std::string& cluster, ResourcePriority priority,
                                           Http::Protocol protocol, LoadBalancerContext* context) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    return nullptr;
  }

  // Select a host and create a connection pool for it if it does not already exist.
  return entry->second->connPool(priority, protocol, context);
}

Tcp::ConnectionPool::Instance*
ClusterManagerImpl::tcpConnPoolForCluster(const std::string& cluster, ResourcePriority priority,
                                          LoadBalancerContext* context) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    return nullptr;
  }

  // Select a host and create a connection pool for it if it does not already exist.
  return entry->second->tcpConnPool(priority, context);
}

void ClusterManagerImpl::postThreadLocalDrainConnections(const Cluster& cluster,
                                                         const HostVector& hosts_removed) {
  tls_->runOnAllThreads([this, name = cluster.info()->name(), hosts_removed]() {
    ThreadLocalClusterManagerImpl::removeHosts(name, hosts_removed, *tls_);
  });
}

void ClusterManagerImpl::postThreadLocalClusterUpdate(const Cluster& cluster, uint32_t priority,
                                                      const HostVector& hosts_added,
                                                      const HostVector& hosts_removed) {
  const auto& host_set = cluster.prioritySet().hostSetsPerPriority()[priority];

  tls_->runOnAllThreads([this, name = cluster.info()->name(), priority,
                         update_params = HostSetImpl::updateHostsParams(*host_set),
                         locality_weights = host_set->localityWeights(), hosts_added, hosts_removed,
                         overprovisioning_factor = host_set->overprovisioningFactor()]() {
    ThreadLocalClusterManagerImpl::updateClusterMembership(
        name, priority, update_params, locality_weights, hosts_added, hosts_removed, *tls_,
        overprovisioning_factor);
  });
}

void ClusterManagerImpl::postThreadLocalHealthFailure(const HostSharedPtr& host) {
  tls_->runOnAllThreads(
      [this, host] { ThreadLocalClusterManagerImpl::onHostHealthFailure(host, *tls_); });
}

Host::CreateConnectionData ClusterManagerImpl::tcpConnForCluster(const std::string& cluster,
                                                                 LoadBalancerContext* context) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    throw EnvoyException(fmt::format("unknown cluster '{}'", cluster));
  }

  HostConstSharedPtr logical_host = entry->second->lb_->chooseHost(context);
  if (logical_host) {
    auto conn_info = logical_host->createConnection(
        cluster_manager.thread_local_dispatcher_, nullptr,
        context == nullptr ? nullptr : context->upstreamTransportSocketOptions());
    if ((entry->second->cluster_info_->features() &
         ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) &&
        conn_info.connection_ != nullptr) {
      auto& conn_map = cluster_manager.host_tcp_conn_map_[logical_host];
      conn_map.emplace(conn_info.connection_.get(),
                       std::make_unique<ThreadLocalClusterManagerImpl::TcpConnContainer>(
                           cluster_manager, logical_host, *conn_info.connection_));
    }
    return conn_info;
  } else {
    entry->second->cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return {nullptr, nullptr};
  }
}

Http::AsyncClient& ClusterManagerImpl::httpAsyncClientForCluster(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();
  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry != cluster_manager.thread_local_clusters_.end()) {
    return entry->second->http_async_client_;
  } else {
    throw EnvoyException(fmt::format("unknown cluster '{}'", cluster));
  }
}

ClusterUpdateCallbacksHandlePtr
ClusterManagerImpl::addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();
  return std::make_unique<ClusterUpdateCallbacksHandleImpl>(cb, cluster_manager.update_callbacks_);
}

ProtobufTypes::MessagePtr ClusterManagerImpl::dumpClusterConfigs() {
  auto config_dump = std::make_unique<envoy::admin::v2alpha::ClustersConfigDump>();
  config_dump->set_version_info(cds_api_ != nullptr ? cds_api_->versionInfo() : "");
  for (const auto& active_cluster_pair : active_clusters_) {
    const auto& cluster = *active_cluster_pair.second;
    if (!cluster.added_via_api_) {
      auto& static_cluster = *config_dump->mutable_static_clusters()->Add();
      static_cluster.mutable_cluster()->MergeFrom(cluster.cluster_config_);
      TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                            *(static_cluster.mutable_last_updated()));
    } else {
      auto& dynamic_cluster = *config_dump->mutable_dynamic_active_clusters()->Add();
      dynamic_cluster.set_version_info(cluster.version_info_);
      dynamic_cluster.mutable_cluster()->MergeFrom(cluster.cluster_config_);
      TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                            *(dynamic_cluster.mutable_last_updated()));
    }
  }

  for (const auto& warming_cluster_pair : warming_clusters_) {
    const auto& cluster = *warming_cluster_pair.second;
    auto& dynamic_cluster = *config_dump->mutable_dynamic_warming_clusters()->Add();
    dynamic_cluster.set_version_info(cluster.version_info_);
    dynamic_cluster.mutable_cluster()->MergeFrom(cluster.cluster_config_);
    TimestampUtil::systemClockToTimestamp(cluster.last_updated_,
                                          *(dynamic_cluster.mutable_last_updated()));
  }

  return config_dump;
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ThreadLocalClusterManagerImpl(
    ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
    const absl::optional<std::string>& local_cluster_name)
    : parent_(parent), thread_local_dispatcher_(dispatcher) {
  // If local cluster is defined then we need to initialize it first.
  if (local_cluster_name) {
    ENVOY_LOG(debug, "adding TLS local cluster {}", local_cluster_name.value());
    auto& local_cluster = parent.active_clusters_.at(local_cluster_name.value());
    thread_local_clusters_[local_cluster_name.value()] = std::make_unique<ClusterEntry>(
        *this, local_cluster->cluster_->info(), local_cluster->loadBalancerFactory());
  }

  local_priority_set_ = local_cluster_name
                            ? &thread_local_clusters_[local_cluster_name.value()]->priority_set_
                            : nullptr;

  for (auto& cluster : parent.active_clusters_) {
    // If local cluster name is set then we already initialized this cluster.
    if (local_cluster_name && local_cluster_name.value() == cluster.first) {
      continue;
    }

    ENVOY_LOG(debug, "adding TLS initial cluster {}", cluster.first);
    ASSERT(thread_local_clusters_.count(cluster.first) == 0);
    thread_local_clusters_[cluster.first] = std::make_unique<ClusterEntry>(
        *this, cluster.second->cluster_->info(), cluster.second->loadBalancerFactory());
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
    if (&cluster.second->priority_set_ != local_priority_set_) {
      cluster.second.reset();
    }
  }
  thread_local_clusters_.clear();
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
        drainTcpConnPools(host, container->second);
      }
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    HostSharedPtr old_host, ConnPoolsContainer& container) {
  container.drains_remaining_ += container.pools_->size();

  // Make a copy to protect against erasure in the callback.
  std::shared_ptr<ConnPoolsContainer::ConnPools> pools = container.pools_;
  pools->addDrainedCallback([this, old_host]() -> void {
    if (destroying_) {
      // It is possible for a connection pool to fire drain callbacks during destruction. Instead
      // of checking if old_host actually exists in the map, it's clearer and cleaner to keep
      // track of destruction as a separate state and check for it here. This also allows us to
      // do this check here versus inside every different connection pool implementation.
      return;
    }

    ConnPoolsContainer* to_clear = getHttpConnPoolsContainer(old_host);
    if (to_clear == nullptr) {
      // This could happen if we have cleaned out the host before iterating through every connection
      // pool. Handle it by just continuing.
      return;
    }

    ASSERT(to_clear->drains_remaining_ > 0);
    to_clear->drains_remaining_--;
    if (to_clear->drains_remaining_ == 0 && to_clear->ready_to_drain_) {
      clearContainer(old_host, *to_clear);
    }
  });

  // We need to hold off on actually emptying out the container until we have finished processing
  // `addDrainedCallback`. If we do not, then it's possible that the container could be erased in
  // the middle of its iteration, which leads to undefined behaviour. We handle that case by
  // checking here to see if the drains have completed.
  container.ready_to_drain_ = true;
  if (container.drains_remaining_ == 0) {
    clearContainer(old_host, container);
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::clearContainer(
    HostSharedPtr old_host, ConnPoolsContainer& container) {
  container.pools_->clear();
  host_http_conn_pool_map_.erase(old_host);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainTcpConnPools(
    HostSharedPtr old_host, TcpConnPoolsContainer& container) {
  container.drains_remaining_ += container.pools_.size();

  for (const auto& pair : container.pools_) {
    pair.second->addDrainedCallback([this, old_host]() -> void {
      if (destroying_) {
        // It is possible for a connection pool to fire drain callbacks during destruction. Instead
        // of checking if old_host actually exists in the map, it's clearer and cleaner to keep
        // track of destruction as a separate state and check for it here. This also allows us to
        // do this check here versus inside every different connection pool implementation.
        return;
      }

      TcpConnPoolsContainer& container = host_tcp_conn_pool_map_[old_host];
      ASSERT(container.drains_remaining_ > 0);
      container.drains_remaining_--;
      if (container.drains_remaining_ == 0) {
        for (auto& pair : container.pools_) {
          thread_local_dispatcher_.deferredDelete(std::move(pair.second));
        }
        host_tcp_conn_pool_map_.erase(old_host);
      }
    });

    // The above addDrainedCallback() drain completion callback might execute immediately. This can
    // then effectively nuke 'container', which means we can't continue to loop on its contents
    // (we're done here).
    if (host_tcp_conn_pool_map_.count(old_host) == 0) {
      break;
    }
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

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::removeHosts(const std::string& name,
                                                                    const HostVector& hosts_removed,
                                                                    ThreadLocal::Slot& tls) {
  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  const auto& cluster_entry = config.thread_local_clusters_[name];
  ENVOY_LOG(debug, "removing hosts for TLS cluster {} removed {}", name, hosts_removed.size());

  // We need to go through and purge any connection pools for hosts that got deleted.
  // Even if two hosts actually point to the same address this will be safe, since if a
  // host is readded it will be a different physical HostSharedPtr.
  cluster_entry->parent_.drainConnPools(hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, uint32_t priority, PrioritySet::UpdateHostsParams update_hosts_params,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, ThreadLocal::Slot& tls, uint64_t overprovisioning_factor) {

  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  const auto& cluster_entry = config.thread_local_clusters_[name];
  ENVOY_LOG(debug, "membership update for TLS cluster {} added {} removed {}", name,
            hosts_added.size(), hosts_removed.size());
  cluster_entry->priority_set_.updateHosts(priority, std::move(update_hosts_params),
                                           std::move(locality_weights), hosts_added, hosts_removed,
                                           overprovisioning_factor);

  // If an LB is thread aware, create a new worker local LB on membership changes.
  if (cluster_entry->lb_factory_ != nullptr) {
    ENVOY_LOG(debug, "re-creating local LB for TLS cluster {}", name);
    cluster_entry->lb_ = cluster_entry->lb_factory_->create();
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure(
    const HostSharedPtr& host, ThreadLocal::Slot& tls) {

  // Drain all HTTP connection pool connections in the case of a host health failure. If outlier/
  // health is due to ECMP flow hashing issues for example, a new set of connections might do
  // better.
  // TODO(mattklein123): This function is currently very specific, but in the future when we do
  // more granular host set changes, we should be able to capture single host changes and make them
  // more targeted.
  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();
  {
    const auto container = config.getHttpConnPoolsContainer(host);
    if (container != nullptr) {
      container->pools_->drainConnections();
    }
  }
  {
    const auto& container = config.host_tcp_conn_pool_map_.find(host);
    if (container != config.host_tcp_conn_pool_map_.end()) {
      for (const auto& pair : container->second.pools_) {
        const Tcp::ConnectionPool::InstancePtr& pool = pair.second;
        pool->drainConnections();
      }
    }
  }

  if (host->cluster().features() &
      ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE) {

    // Each connection will remove itself from the TcpConnectionsMap when it closes, via its
    // Network::ConnectionCallbacks. The last removed tcp conn will remove the TcpConnectionsMap
    // from host_tcp_conn_map_, so do not cache it between iterations.
    //
    // TODO(ggreenway) PERF: If there are a large number of connections, this could take a long time
    // and halt other useful work. Consider breaking up this work. Note that this behavior is noted
    // in the configuration documentation in cluster setting
    // "close_connections_on_host_health_failure". Update the docs if this if this changes.
    while (true) {
      const auto& it = config.host_tcp_conn_map_.find(host);
      if (it == config.host_tcp_conn_map_.end()) {
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
                         parent_.parent_.http_context_) {
  priority_set_.getOrCreateHostSet(0);

  // TODO(mattklein123): Consider converting other LBs over to thread local. All of them could
  // benefit given the healthy panic, locality, and priority calculations that take place.
  if (cluster->lbSubsetInfo().isEnabled()) {
    lb_ = std::make_unique<SubsetLoadBalancer>(
        cluster->lbType(), priority_set_, parent_.local_priority_set_, cluster->stats(),
        cluster->statsScope(), parent.parent_.runtime_, parent.parent_.random_,
        cluster->lbSubsetInfo(), cluster->lbRingHashConfig(), cluster->lbLeastRequestConfig(),
        cluster->lbConfig());
  } else {
    switch (cluster->lbType()) {
    case LoadBalancerType::LeastRequest: {
      ASSERT(lb_factory_ == nullptr);
      lb_ = std::make_unique<LeastRequestLoadBalancer>(
          priority_set_, parent_.local_priority_set_, cluster->stats(), parent.parent_.runtime_,
          parent.parent_.random_, cluster->lbConfig(), cluster->lbLeastRequestConfig());
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
      lb_ = std::make_unique<RoundRobinLoadBalancer>(priority_set_, parent_.local_priority_set_,
                                                     cluster->stats(), parent.parent_.runtime_,
                                                     parent.parent_.random_, cluster->lbConfig());
      break;
    }
    case LoadBalancerType::ClusterProvided:
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

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::~ClusterEntry() {
  // We need to drain all connection pools for the cluster being removed. Then we can remove the
  // cluster.
  //
  // TODO(mattklein123): Optimally, we would just fire member changed callbacks and remove all of
  // the hosts inside of the HostImpl destructor. That is a change with wide implications, so we are
  // going with a more targeted approach for now.
  for (auto& host_set : priority_set_.hostSetsPerPriority()) {
    parent_.drainConnPools(host_set->hosts());
  }
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::connPool(
    ResourcePriority priority, Http::Protocol protocol, LoadBalancerContext* context) {
  HostConstSharedPtr host = lb_->chooseHost(context);
  if (!host) {
    ENVOY_LOG(debug, "no healthy host for HTTP connection pool");
    cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return nullptr;
  }

  std::vector<uint8_t> hash_key = {uint8_t(protocol)};

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
    context->upstreamTransportSocketOptions()->hashKey(hash_key);
    have_transport_socket_options = true;
  }

  ConnPoolsContainer& container = *parent_.getHttpConnPoolsContainer(host, true);

  // Note: to simplify this, we assume that the factory is only called in the scope of this
  // function. Otherwise, we'd need to capture a few of these variables by value.
  ConnPoolsContainer::ConnPools::OptPoolRef pool =
      container.pools_->getPool(priority, hash_key, [&]() {
        return parent_.parent_.factory_.allocateConnPool(
            parent_.thread_local_dispatcher_, host, priority, protocol,
            !upstream_options->empty() ? upstream_options : nullptr,
            have_transport_socket_options ? context->upstreamTransportSocketOptions() : nullptr);
      });

  if (pool.has_value()) {
    return &(pool.value().get());
  } else {
    return nullptr;
  }
}

Tcp::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::tcpConnPool(
    ResourcePriority priority, LoadBalancerContext* context) {
  HostConstSharedPtr host = lb_->chooseHost(context);
  if (!host) {
    ENVOY_LOG(debug, "no healthy host for TCP connection pool");
    cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return nullptr;
  }

  // Inherit socket options from downstream connection, if set.
  std::vector<uint8_t> hash_key = {uint8_t(priority)};

  // Use downstream connection socket options for computing connection pool hash key, if any.
  // This allows socket options to control connection pooling so that connections with
  // different options are not pooled together.
  bool have_options = false;
  if (context != nullptr && context->downstreamConnection()) {
    const Network::ConnectionSocket::OptionsSharedPtr& options =
        context->downstreamConnection()->socketOptions();
    if (options) {
      for (const auto& option : *options) {
        have_options = true;
        option->hashKey(hash_key);
      }
    }
  }

  bool have_transport_socket_options = false;
  if (context != nullptr && context->upstreamTransportSocketOptions() != nullptr) {
    have_transport_socket_options = true;
    context->upstreamTransportSocketOptions()->hashKey(hash_key);
  }

  TcpConnPoolsContainer& container = parent_.host_tcp_conn_pool_map_[host];
  if (!container.pools_[hash_key]) {
    container.pools_[hash_key] = parent_.parent_.factory_.allocateTcpConnPool(
        parent_.thread_local_dispatcher_, host, priority,
        have_options ? context->downstreamConnection()->socketOptions() : nullptr,
        have_transport_socket_options ? context->upstreamTransportSocketOptions() : nullptr);
  }

  return container.pools_[hash_key].get();
}

ClusterManagerPtr ProdClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return ClusterManagerPtr{new ClusterManagerImpl(
      bootstrap, *this, stats_, tls_, runtime_, random_, local_info_, log_manager_,
      main_thread_dispatcher_, admin_, validation_context_, api_, http_context_)};
}

Http::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    Http::Protocol protocol, const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsSharedPtr& transport_socket_options) {
  if (protocol == Http::Protocol::Http2 &&
      runtime_.snapshot().featureEnabled("upstream.use_http2", 100)) {
    return std::make_unique<Http::Http2::ProdConnPoolImpl>(dispatcher, host, priority, options,
                                                           transport_socket_options);
  } else if (protocol == Http::Protocol::Http3) {
    // Quic connection pool is not implemented.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  } else {
    return std::make_unique<Http::Http1::ProdConnPoolImpl>(dispatcher, host, priority, options,
                                                           transport_socket_options);
  }
}

Tcp::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateTcpConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsSharedPtr transport_socket_options) {
  return Tcp::ConnectionPool::InstancePtr{
      new Tcp::ConnPoolImpl(dispatcher, host, priority, options, transport_socket_options)};
}

std::pair<ClusterSharedPtr, ThreadAwareLoadBalancerPtr> ProdClusterManagerFactory::clusterFromProto(
    const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) {
  return ClusterFactoryImplBase::create(
      cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_, runtime_, random_,
      main_thread_dispatcher_, log_manager_, local_info_, admin_, singleton_manager_,
      outlier_event_logger, added_via_api,
      added_via_api ? validation_context_.dynamicValidationVisitor()
                    : validation_context_.staticValidationVisitor(),
      api_);
}

CdsApiPtr ProdClusterManagerFactory::createCds(const envoy::api::v2::core::ConfigSource& cds_config,
                                               ClusterManager& cm) {
  // TODO(htuch): Differentiate static vs. dynamic validation visitors.
  return CdsApiImpl::create(cds_config, cm, stats_, validation_context_.dynamicValidationVisitor());
}

} // namespace Upstream
} // namespace Envoy
