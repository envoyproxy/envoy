#include "common/upstream/cluster_manager_impl.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/config/cds_json.h"
#include "common/config/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/grpc/async_client_manager_impl.h"
#include "common/http/async_client_impl.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/json/config_schemas.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/utility.h"
#include "common/router/shadow_writer_impl.h"
#include "common/upstream/cds_api_impl.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/maglev_lb.h"
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/subset_lb.h"

namespace Envoy {
namespace Upstream {

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
      started_secondary_initialize_ = true;
      // Cluster::initialize() method can modify the list of secondary_init_clusters_ to remove
      // the item currently being initialized, so we eschew range-based-for and do this complicated
      // dance to increment the iterator before calling initialize.
      for (auto iter = secondary_init_clusters_.begin(); iter != secondary_init_clusters_.end();) {
        Cluster* cluster = *iter;
        ++iter;
        cluster->initialize([cluster, this] { onClusterInit(*cluster); });
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

ClusterManagerImpl::ClusterManagerImpl(const envoy::config::bootstrap::v2::Bootstrap& bootstrap,
                                       ClusterManagerFactory& factory, Stats::Store& stats,
                                       ThreadLocal::Instance& tls, Runtime::Loader& runtime,
                                       Runtime::RandomGenerator& random,
                                       const LocalInfo::LocalInfo& local_info,
                                       AccessLog::AccessLogManager& log_manager,
                                       Event::Dispatcher& main_thread_dispatcher)
    : factory_(factory), runtime_(runtime), stats_(stats), tls_(tls.allocateSlot()),
      random_(random), bind_config_(bootstrap.cluster_manager().upstream_bind_config()),
      local_info_(local_info), cm_stats_(generateStats(stats)),
      init_helper_([this](Cluster& cluster) { onClusterInit(cluster); }) {
  async_client_manager_ = std::make_unique<Grpc::AsyncClientManagerImpl>(*this, tls);
  const auto& cm_config = bootstrap.cluster_manager();
  if (cm_config.has_outlier_detection()) {
    const std::string event_log_file_path = cm_config.outlier_detection().event_log_path();
    if (!event_log_file_path.empty()) {
      outlier_event_logger_.reset(new Outlier::EventLoggerImpl(log_manager, event_log_file_path,
                                                               ProdSystemTimeSource::instance_,
                                                               ProdMonotonicTimeSource::instance_));
    }
  }

  if (bootstrap.dynamic_resources().deprecated_v1().has_sds_config()) {
    eds_config_ = bootstrap.dynamic_resources().deprecated_v1().sds_config();
  }

  // Cluster loading happens in two phases: first all the primary clusters are loaded, and then all
  // the secondary clusters are loaded. As it currently stands all non-EDS clusters are primary and
  // only EDS clusters are secondary. This two phase loading is done because in v2 configuration
  // each EDS cluster individually sets up a subscription. When this subscription is an API source
  // the cluster will depend on a non-EDS cluster, so the non-EDS clusters must be loaded first.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // First load all the primary clusters.
    if (cluster.type() != envoy::api::v2::Cluster::EDS) {
      loadCluster(cluster, false, active_clusters_);
    }
  }

  // Now setup ADS if needed, this might rely on a primary cluster.
  if (bootstrap.dynamic_resources().has_ads_config()) {
    ads_mux_.reset(new Config::GrpcMuxImpl(
        bootstrap.node(),
        Config::Utility::factoryForApiConfigSource(
            *async_client_manager_, bootstrap.dynamic_resources().ads_config(), stats)
            ->create(),
        main_thread_dispatcher,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.service.discovery.v2.AggregatedDiscoveryService.StreamAggregatedResources")));
  } else {
    ads_mux_.reset(new Config::NullGrpcMuxImpl());
  }

  // After ADS is initialized, load EDS static clusters as EDS config may potentially need ADS.
  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    // Now load all the secondary clusters.
    if (cluster.type() == envoy::api::v2::Cluster::EDS) {
      loadCluster(cluster, false, active_clusters_);
    }
  }

  cm_stats_.cluster_added_.add(bootstrap.static_resources().clusters().size());
  updateGauges();

  // All the static clusters have been loaded. At this point we can check for the
  // existence of the v1 sds backing cluster, and the ads backing cluster.
  // TODO(htuch): Add support for multiple clusters, #1170.
  const ClusterInfoMap loaded_clusters = clusters();
  if (bootstrap.dynamic_resources().deprecated_v1().has_sds_config()) {
    const auto& sds_config = bootstrap.dynamic_resources().deprecated_v1().sds_config();
    switch (sds_config.config_source_specifier_case()) {
    case envoy::api::v2::core::ConfigSource::kPath: {
      Config::Utility::checkFilesystemSubscriptionBackingPath(sds_config.path());
      break;
    }
    case envoy::api::v2::core::ConfigSource::kApiConfigSource: {
      Config::Utility::checkApiConfigSourceSubscriptionBackingCluster(
          loaded_clusters, sds_config.api_config_source());
      break;
    }
    case envoy::api::v2::core::ConfigSource::kAds: {
      // Backing cluster will be checked below
      break;
    }
    default:
      // Validated by schema.
      NOT_REACHED;
    }
  }

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
  if (bootstrap.dynamic_resources().has_cds_config()) {
    cds_api_ = factory_.createCds(bootstrap.dynamic_resources().cds_config(), eds_config_, *this);
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
    load_stats_reporter_.reset(new LoadStatsReporter(
        bootstrap.node(), *this, stats,
        Config::Utility::factoryForApiConfigSource(*async_client_manager_, load_stats_config, stats)
            ->create(),
        main_thread_dispatcher));
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
  cluster.prioritySet().addMemberUpdateCb([&cluster, this](uint32_t priority,
                                                           const HostVector& hosts_added,
                                                           const HostVector& hosts_removed) {
    // This fires when a cluster is about to have an updated member set. We need to send this
    // out to all of the thread local configurations.
    postThreadLocalClusterUpdate(cluster, priority, hosts_added, hosts_removed);
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

bool ClusterManagerImpl::addOrUpdateCluster(const envoy::api::v2::Cluster& cluster) {
  // First we need to see if this new config is new or an update to an existing dynamic cluster.
  // We don't allow updates to statically configured clusters in the main configuration. We check
  // both the warming clusters and the active clusters to see if we need an update or the update
  // should be blocked.
  const std::string cluster_name = cluster.name();
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
    // The following init manager remove call is a NOP in the case we are already initialized. It's
    // just kept here to avoid additional logic.
    init_helper_.removeCluster(*existing_active_cluster->second->cluster_);
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
  loadCluster(cluster, true, use_active_map ? active_clusters_ : warming_clusters_);

  if (use_active_map) {
    ENVOY_LOG(info, "add/update cluster {} during init", cluster_name);
    auto& cluster_entry = active_clusters_.at(cluster_name);
    createOrUpdateThreadLocalCluster(*cluster_entry);
    init_helper_.addCluster(*cluster_entry->cluster_);
  } else {
    auto& cluster_entry = warming_clusters_.at(cluster_name);
    ENVOY_LOG(info, "add/update cluster {} starting warming", cluster_name);
    cluster_entry->cluster_->initialize([this, cluster_name] {
      auto warming_it = warming_clusters_.find(cluster_name);
      auto& cluster_entry = *warming_it->second;
      active_clusters_[cluster_name] = std::move(warming_it->second);
      warming_clusters_.erase(warming_it);

      ENVOY_LOG(info, "warming cluster {} complete", cluster_name);
      createOrUpdateThreadLocalCluster(cluster_entry);
      onClusterInit(*cluster_entry.cluster_);
      updateGauges();
    });
  }

  updateGauges();
  return true;
}

void ClusterManagerImpl::createOrUpdateThreadLocalCluster(ClusterData& cluster) {
  tls_->runOnAllThreads(
      [
        this, new_cluster = cluster.cluster_->info(),
        thread_aware_lb_factory = cluster.loadBalancerFactory()
      ]()
          ->void {
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
      cluster_manager.thread_local_clusters_.erase(cluster_name);
      for (auto& cb : cluster_manager.update_callbacks_) {
        cb->onClusterRemoval(cluster_name);
      }
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
    updateGauges();
  }

  return removed;
}

void ClusterManagerImpl::loadCluster(const envoy::api::v2::Cluster& cluster, bool added_via_api,
                                     ClusterMap& cluster_map) {
  ClusterSharedPtr new_cluster =
      factory_.clusterFromProto(cluster, *this, outlier_event_logger_, added_via_api);

  if (!added_via_api) {
    if (cluster_map.find(new_cluster->info()->name()) != cluster_map.end()) {
      throw EnvoyException(
          fmt::format("cluster manager: duplicate cluster '{}'", new_cluster->info()->name()));
    }
  }

  Cluster& cluster_reference = *new_cluster;
  if (new_cluster->healthChecker() != nullptr) {
    new_cluster->healthChecker()->addHostCheckCompleteCb(
        [this](HostSharedPtr host, bool changed_state) {
          if (changed_state && host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
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
      MessageUtil::hash(cluster), added_via_api, std::move(new_cluster));
  const auto cluster_entry_it = cluster_map.find(cluster_reference.info()->name());

  // If an LB is thread aware, create it here. The LB is not initialized until cluster pre-init
  // finishes.
  if (cluster_reference.info()->lbType() == LoadBalancerType::RingHash) {
    cluster_entry_it->second->thread_aware_lb_ = std::make_unique<RingHashLoadBalancer>(
        cluster_reference.prioritySet(), cluster_reference.info()->stats(), runtime_, random_,
        cluster_reference.info()->lbRingHashConfig(), cluster_reference.info()->lbConfig());
  } else if (cluster_reference.info()->lbType() == LoadBalancerType::Maglev) {
    cluster_entry_it->second->thread_aware_lb_ = std::make_unique<MaglevLoadBalancer>(
        cluster_reference.prioritySet(), cluster_reference.info()->stats(), runtime_, random_,
        cluster_reference.info()->lbConfig());
  }

  updateGauges();
}

void ClusterManagerImpl::updateGauges() {
  cm_stats_.active_clusters_.set(active_clusters_.size());
  cm_stats_.warming_clusters_.set(warming_clusters_.size());
}

ThreadLocalCluster* ClusterManagerImpl::get(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

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

void ClusterManagerImpl::postThreadLocalClusterUpdate(const Cluster& cluster, uint32_t priority,
                                                      const HostVector& hosts_added,
                                                      const HostVector& hosts_removed) {
  const auto& host_set = cluster.prioritySet().hostSetsPerPriority()[priority];

  // TODO(htuch): Can we skip these copies by exporting out const shared_ptr from HostSet?
  HostVectorConstSharedPtr hosts_copy(new HostVector(host_set->hosts()));
  HostVectorConstSharedPtr healthy_hosts_copy(new HostVector(host_set->healthyHosts()));
  HostsPerLocalityConstSharedPtr hosts_per_locality_copy = host_set->hostsPerLocality().clone();
  HostsPerLocalityConstSharedPtr healthy_hosts_per_locality_copy =
      host_set->healthyHostsPerLocality().clone();

  tls_->runOnAllThreads([
    this, name = cluster.info()->name(), priority, hosts_copy, healthy_hosts_copy,
    hosts_per_locality_copy, healthy_hosts_per_locality_copy,
    locality_weights = host_set->localityWeights(), hosts_added, hosts_removed
  ]() {
    ThreadLocalClusterManagerImpl::updateClusterMembership(
        name, priority, hosts_copy, healthy_hosts_copy, hosts_per_locality_copy,
        healthy_hosts_per_locality_copy, locality_weights, hosts_added, hosts_removed, *tls_);
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
    return logical_host->createConnection(cluster_manager.thread_local_dispatcher_, nullptr);
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

const std::string ClusterManagerImpl::versionInfo() const {
  if (cds_api_) {
    return cds_api_->versionInfo();
  }
  return "static";
}

ClusterUpdateCallbacksHandlePtr
ClusterManagerImpl::addThreadLocalClusterUpdateCallbacks(ClusterUpdateCallbacks& cb) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();
  return std::make_unique<ClusterUpdateCallbacksHandleImpl>(cb, cluster_manager.update_callbacks_);
}

ClusterManagerImpl::ClusterUpdateCallbacksHandleImpl::ClusterUpdateCallbacksHandleImpl(
    ClusterUpdateCallbacks& cb, std::list<ClusterUpdateCallbacks*>& ll)
    : list(ll) {
  entry = ll.emplace(ll.begin(), &cb);
}

ClusterManagerImpl::ClusterUpdateCallbacksHandleImpl::~ClusterUpdateCallbacksHandleImpl() {
  ASSERT(std::find(list.begin(), list.end(), *entry) != list.end());
  list.erase(entry);
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ThreadLocalClusterManagerImpl(
    ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
    const absl::optional<std::string>& local_cluster_name)
    : parent_(parent), thread_local_dispatcher_(dispatcher) {
  // If local cluster is defined then we need to initialize it first.
  if (local_cluster_name) {
    ENVOY_LOG(debug, "adding TLS local cluster {}", local_cluster_name.value());
    auto& local_cluster = parent.active_clusters_.at(local_cluster_name.value());
    thread_local_clusters_[local_cluster_name.value()].reset(new ClusterEntry(
        *this, local_cluster->cluster_->info(), local_cluster->loadBalancerFactory()));
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
    thread_local_clusters_[cluster.first].reset(new ClusterEntry(
        *this, cluster.second->cluster_->info(), cluster.second->loadBalancerFactory()));
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::~ThreadLocalClusterManagerImpl() {
  // Clear out connection pools as well as the thread local cluster map so that we release all
  // cluster pointers. Currently we have to free all non-local clusters before we free
  // the local cluster. This is because non-local clusters have a member update callback registered
  // with the local cluster.
  // TODO(mattklein123): The above is sub-optimal and is related to the TODO in
  //                     redis/conn_pool_impl.cc. Will fix at the same time.
  ENVOY_LOG(debug, "shutting down thread local cluster manager");
  host_http_conn_pool_map_.clear();
  for (auto& cluster : thread_local_clusters_) {
    if (&cluster.second->priority_set_ != local_priority_set_) {
      cluster.second.reset();
    }
  }
  thread_local_clusters_.clear();
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(const HostVector& hosts) {
  for (const HostSharedPtr& host : hosts) {
    auto container = host_http_conn_pool_map_.find(host);
    if (container != host_http_conn_pool_map_.end()) {
      drainConnPools(host, container->second);
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    HostSharedPtr old_host, ConnPoolsContainer& container) {
  container.drains_remaining_ += container.pools_.size();

  for (const auto& pair : container.pools_) {
    pair.second->addDrainedCallback([this, old_host]() -> void {
      ConnPoolsContainer& container = host_http_conn_pool_map_[old_host];
      ASSERT(container.drains_remaining_ > 0);
      container.drains_remaining_--;
      if (container.drains_remaining_ == 0) {
        for (auto& pair : container.pools_) {
          thread_local_dispatcher_.deferredDelete(std::move(pair.second));
        }
        host_http_conn_pool_map_.erase(old_host);
      }
    });

    // The above addDrainedCallback() drain completion callback might execute immediately. This can
    // then effectively nuke 'container', which means we can't continue to loop on its contents
    // (we're done here).
    if (host_http_conn_pool_map_.count(old_host) == 0) {
      break;
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, uint32_t priority, HostVectorConstSharedPtr hosts,
    HostVectorConstSharedPtr healthy_hosts, HostsPerLocalityConstSharedPtr hosts_per_locality,
    HostsPerLocalityConstSharedPtr healthy_hosts_per_locality,
    LocalityWeightsConstSharedPtr locality_weights, const HostVector& hosts_added,
    const HostVector& hosts_removed, ThreadLocal::Slot& tls) {

  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  const auto& cluster_entry = config.thread_local_clusters_[name];
  ENVOY_LOG(debug, "membership update for TLS cluster {}", name);
  cluster_entry->priority_set_.getOrCreateHostSet(priority).updateHosts(
      std::move(hosts), std::move(healthy_hosts), std::move(hosts_per_locality),
      std::move(healthy_hosts_per_locality), std::move(locality_weights), hosts_added,
      hosts_removed);

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
  const auto& container = config.host_http_conn_pool_map_.find(host);
  if (container != config.host_http_conn_pool_map_.end()) {
    for (const auto& pair : container->second.pools_) {
      const Http::ConnectionPool::InstancePtr& pool = pair.second;
      pool->drainConnections();
    }
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster,
    const LoadBalancerFactorySharedPtr& lb_factory)
    : parent_(parent), lb_factory_(lb_factory), cluster_info_(cluster),
      http_async_client_(*cluster, parent.parent_.stats_, parent.thread_local_dispatcher_,
                         parent.parent_.local_info_, parent.parent_, parent.parent_.runtime_,
                         parent.parent_.random_,
                         Router::ShadowWriterPtr{new Router::ShadowWriterImpl(parent.parent_)}) {
  priority_set_.getOrCreateHostSet(0);

  // TODO(mattklein123): Consider converting other LBs over to thread local. All of them could
  // benefit given the healthy panic, locality, and priority calculations that take place.
  if (cluster->lbSubsetInfo().isEnabled()) {
    ASSERT(lb_factory_ == nullptr);
    lb_.reset(new SubsetLoadBalancer(cluster->lbType(), priority_set_, parent_.local_priority_set_,
                                     cluster->stats(), parent.parent_.runtime_,
                                     parent.parent_.random_, cluster->lbSubsetInfo(),
                                     cluster->lbRingHashConfig(), cluster->lbConfig()));
  } else {
    switch (cluster->lbType()) {
    case LoadBalancerType::LeastRequest: {
      ASSERT(lb_factory_ == nullptr);
      lb_.reset(new LeastRequestLoadBalancer(priority_set_, parent_.local_priority_set_,
                                             cluster->stats(), parent.parent_.runtime_,
                                             parent.parent_.random_, cluster->lbConfig()));
      break;
    }
    case LoadBalancerType::Random: {
      ASSERT(lb_factory_ == nullptr);
      lb_.reset(new RandomLoadBalancer(priority_set_, parent_.local_priority_set_, cluster->stats(),
                                       parent.parent_.runtime_, parent.parent_.random_,
                                       cluster->lbConfig()));
      break;
    }
    case LoadBalancerType::RoundRobin: {
      ASSERT(lb_factory_ == nullptr);
      lb_.reset(new RoundRobinLoadBalancer(priority_set_, parent_.local_priority_set_,
                                           cluster->stats(), parent.parent_.runtime_,
                                           parent.parent_.random_, cluster->lbConfig()));
      break;
    }
    case LoadBalancerType::RingHash:
    case LoadBalancerType::Maglev: {
      ASSERT(lb_factory_ != nullptr);
      lb_ = lb_factory_->create();
      break;
    }
    case LoadBalancerType::OriginalDst: {
      ASSERT(lb_factory_ == nullptr);
      lb_.reset(new OriginalDstCluster::LoadBalancer(
          priority_set_, parent.parent_.active_clusters_.at(cluster->name())->cluster_));
      break;
    }
    }
  }

  priority_set_.addMemberUpdateCb(
      [this](uint32_t, const HostVector&, const HostVector& hosts_removed) -> void {
        // We need to go through and purge any connection pools for hosts that got deleted.
        // Even if two hosts actually point to the same address this will be safe, since if a
        // host is readded it will be a different physical HostSharedPtr.
        parent_.drainConnPools(hosts_removed);
      });
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

  // Inherit socket options from downstream connection, if set.
  std::vector<uint8_t> hash_key = {uint8_t(protocol), uint8_t(priority)};

  // Use downstream connection socket options for computing connection pool hash key, if any.
  // This allows socket options to control connection pooling so that connections with
  // different options are not pooled together.
  bool have_options = false;
  if (context && context->downstreamConnection()) {
    const Network::ConnectionSocket::OptionsSharedPtr& options =
        context->downstreamConnection()->socketOptions();
    if (options) {
      for (const auto& option : *options) {
        have_options = true;
        option->hashKey(hash_key);
      }
    }
  }

  ConnPoolsContainer& container = parent_.host_http_conn_pool_map_[host];
  if (!container.pools_[hash_key]) {
    container.pools_[hash_key] = parent_.parent_.factory_.allocateConnPool(
        parent_.thread_local_dispatcher_, host, priority, protocol,
        have_options ? context->downstreamConnection()->socketOptions() : nullptr);
  }

  return container.pools_[hash_key].get();
}

ClusterManagerPtr ProdClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v2::Bootstrap& bootstrap, Stats::Store& stats,
    ThreadLocal::Instance& tls, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager) {
  return ClusterManagerPtr{new ClusterManagerImpl(bootstrap, *this, stats, tls, runtime, random,
                                                  local_info, log_manager,
                                                  main_thread_dispatcher_)};
}

Http::ConnectionPool::InstancePtr ProdClusterManagerFactory::allocateConnPool(
    Event::Dispatcher& dispatcher, HostConstSharedPtr host, ResourcePriority priority,
    Http::Protocol protocol, const Network::ConnectionSocket::OptionsSharedPtr& options) {
  if (protocol == Http::Protocol::Http2 &&
      runtime_.snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http2::ProdConnPoolImpl(dispatcher, host, priority, options)};
  } else {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http1::ConnPoolImplProd(dispatcher, host, priority, options)};
  }
}

ClusterSharedPtr ProdClusterManagerFactory::clusterFromProto(
    const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) {
  return ClusterImplBase::create(cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_,
                                 runtime_, random_, main_thread_dispatcher_, local_info_,
                                 outlier_event_logger, added_via_api);
}

CdsApiPtr ProdClusterManagerFactory::createCds(
    const envoy::api::v2::core::ConfigSource& cds_config,
    const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config, ClusterManager& cm) {
  return CdsApiImpl::create(cds_config, eds_config, cm, main_thread_dispatcher_, random_,
                            local_info_, stats_);
}

} // namespace Upstream
} // namespace Envoy
