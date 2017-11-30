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
#include "common/common/utility.h"
#include "common/config/cds_json.h"
#include "common/config/utility.h"
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
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/subset_lb.h"

#include "fmt/format.h"

namespace Envoy {
namespace Upstream {

void ClusterManagerInitHelper::addCluster(Cluster& cluster) {
  if (state_ == State::AllClustersInitialized) {
    cluster.initialize([] {});
    return;
  }

  const auto initialize_cb = [&cluster, this] {
    ASSERT(state_ != State::AllClustersInitialized);
    removeCluster(cluster);
  };

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
        cluster->initialize([cluster, this] {
          ASSERT(state_ != State::AllClustersInitialized);
          removeCluster(*cluster);
        });
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

ClusterManagerImpl::ClusterManagerImpl(const envoy::api::v2::Bootstrap& bootstrap,
                                       ClusterManagerFactory& factory, Stats::Store& stats,
                                       ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                                       Runtime::RandomGenerator& random,
                                       const LocalInfo::LocalInfo& local_info,
                                       AccessLog::AccessLogManager& log_manager,
                                       Event::Dispatcher& primary_dispatcher)
    : factory_(factory), runtime_(runtime), stats_(stats), tls_(tls.allocateSlot()),
      random_(random), local_info_(local_info), cm_stats_(generateStats(stats)) {
  const auto& ads_config = bootstrap.dynamic_resources().ads_config();
  if (ads_config.cluster_name().empty()) {
    ENVOY_LOG(debug, "No ADS clusters defined, ADS will not be initialized.");
    ads_mux_.reset(new Config::NullGrpcMuxImpl());
  } else {
    if (ads_config.cluster_name().size() != 1) {
      // TODO(htuch): Add support for multiple clusters, #1170.
      throw EnvoyException(
          "envoy::api::v2::ApiConfigSource must have a singleton cluster name specified");
    }
    ads_mux_.reset(new Config::GrpcMuxImpl(
        bootstrap.node(), *this, ads_config.cluster_name()[0], primary_dispatcher,
        *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
            "envoy.api.v2.AggregatedDiscoveryService.StreamAggregatedResources")));
  }

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
    eds_config_.value(bootstrap.dynamic_resources().deprecated_v1().sds_config());
  }

  if (bootstrap.cluster_manager().upstream_bind_config().has_source_address()) {
    source_address_ = Network::Address::resolveProtoSocketAddress(
        bootstrap.cluster_manager().upstream_bind_config().source_address());
  }

  for (const auto& cluster : bootstrap.static_resources().clusters()) {
    loadCluster(cluster, false);
  }

  // We can now potentially create the CDS API once the backing cluster exists.
  if (bootstrap.dynamic_resources().has_cds_config()) {
    cds_api_ = factory_.createCds(bootstrap.dynamic_resources().cds_config(), eds_config_, *this);
    init_helper_.setCds(cds_api_.get());
  } else {
    init_helper_.setCds(nullptr);
  }

  Optional<std::string> local_cluster_name;
  if (!cm_config.local_cluster_name().empty()) {
    local_cluster_name.value(cm_config.local_cluster_name());
    if (primary_clusters_.find(local_cluster_name.value()) == primary_clusters_.end()) {
      throw EnvoyException(
          fmt::format("local cluster '{}' must be defined", local_cluster_name.value()));
    }
  }

  tls_->set([this, local_cluster_name](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return ThreadLocal::ThreadLocalObjectSharedPtr{
        new ThreadLocalClusterManagerImpl(*this, dispatcher, local_cluster_name)};
  });

  init_helper_.onStaticLoadComplete();

  // To avoid threading issues, for those clusters that start with hosts already in them (like the
  // static cluster), we need to post an update onto each thread to notify them of the update. We
  // also require this for dynamic clusters where an immediate resolve occurred in the cluster
  // constructor, prior to the member update callback being configured.
  for (auto& cluster : primary_clusters_) {
    postInitializeCluster(*cluster.second.cluster_);
  }

  ads_mux_->start();

  if (cm_config.has_load_stats_config()) {
    const auto& load_stats_config = cm_config.load_stats_config();
    if (load_stats_config.cluster_name().size() != 1) {
      // TODO(htuch): Add support for multiple clusters, #1170.
      throw EnvoyException(
          "envoy::api::v2::ApiConfigSource must have a singleton cluster name specified");
    }
    load_stats_reporter_.reset(new LoadStatsReporter(
        bootstrap.node(), *this, stats, load_stats_config.cluster_name()[0], primary_dispatcher));
  }
}

ClusterManagerStats ClusterManagerImpl::generateStats(Stats::Scope& scope) {
  const std::string final_prefix = "cluster_manager.";
  return {ALL_CLUSTER_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix))};
}

void ClusterManagerImpl::postInitializeCluster(Cluster& cluster) {
  if (cluster.hosts().empty()) {
    return;
  }

  postThreadLocalClusterUpdate(cluster, cluster.hosts(), std::vector<HostSharedPtr>{});
}

bool ClusterManagerImpl::addOrUpdatePrimaryCluster(const envoy::api::v2::Cluster& cluster) {
  // First we need to see if this new config is new or an update to an existing dynamic cluster.
  // We don't allow updates to statically configured clusters in the main configuration.
  const std::string cluster_name = cluster.name();
  auto existing_cluster = primary_clusters_.find(cluster_name);
  if (existing_cluster != primary_clusters_.end() &&
      (!existing_cluster->second.added_via_api_ ||
       existing_cluster->second.config_hash_ == MessageUtil::hash(cluster))) {
    return false;
  }

  if (existing_cluster != primary_clusters_.end()) {
    init_helper_.removeCluster(*existing_cluster->second.cluster_);
  }

  loadCluster(cluster, true);
  ClusterInfoConstSharedPtr new_cluster = primary_clusters_.at(cluster_name).cluster_->info();
  ENVOY_LOG(info, "add/update cluster {}", cluster_name);
  tls_->runOnAllThreads([this, new_cluster]() -> void {
    ThreadLocalClusterManagerImpl& cluster_manager =
        tls_->getTyped<ThreadLocalClusterManagerImpl>();

    if (cluster_manager.thread_local_clusters_.count(new_cluster->name()) > 0) {
      ENVOY_LOG(debug, "updating TLS cluster {}", new_cluster->name());
    } else {
      ENVOY_LOG(debug, "adding TLS cluster {}", new_cluster->name());
    }

    cluster_manager.thread_local_clusters_[new_cluster->name()].reset(
        new ThreadLocalClusterManagerImpl::ClusterEntry(cluster_manager, new_cluster));
  });

  postInitializeCluster(*primary_clusters_.at(cluster_name).cluster_);
  return true;
}

bool ClusterManagerImpl::removePrimaryCluster(const std::string& cluster_name) {
  auto existing_cluster = primary_clusters_.find(cluster_name);
  if (existing_cluster == primary_clusters_.end() || !existing_cluster->second.added_via_api_) {
    return false;
  }

  init_helper_.removeCluster(*existing_cluster->second.cluster_);
  primary_clusters_.erase(existing_cluster);
  cm_stats_.cluster_removed_.inc();
  cm_stats_.total_clusters_.set(primary_clusters_.size());
  ENVOY_LOG(info, "removing cluster {}", cluster_name);
  tls_->runOnAllThreads([this, cluster_name]() -> void {
    ThreadLocalClusterManagerImpl& cluster_manager =
        tls_->getTyped<ThreadLocalClusterManagerImpl>();

    ASSERT(cluster_manager.thread_local_clusters_.count(cluster_name) == 1);
    ENVOY_LOG(debug, "removing TLS cluster {}", cluster_name);
    cluster_manager.thread_local_clusters_.erase(cluster_name);
  });

  return true;
}

void ClusterManagerImpl::loadCluster(const envoy::api::v2::Cluster& cluster, bool added_via_api) {
  ClusterSharedPtr new_cluster =
      factory_.clusterFromProto(cluster, *this, outlier_event_logger_, added_via_api);

  init_helper_.addCluster(*new_cluster);
  if (!added_via_api) {
    if (primary_clusters_.find(new_cluster->info()->name()) != primary_clusters_.end()) {
      throw EnvoyException(
          fmt::format("cluster manager: duplicate cluster '{}'", new_cluster->info()->name()));
    }
  }

  const Cluster& primary_cluster_reference = *new_cluster;
  new_cluster->addMemberUpdateCb(
      [&primary_cluster_reference, this](const std::vector<HostSharedPtr>& hosts_added,
                                         const std::vector<HostSharedPtr>& hosts_removed) {
        // This fires when a cluster is about to have an updated member set. We need to send this
        // out to all of the thread local configurations.
        postThreadLocalClusterUpdate(primary_cluster_reference, hosts_added, hosts_removed);
      });

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

  // emplace() will do nothing if the key already exists. Always erase first.
  size_t num_erased = primary_clusters_.erase(primary_cluster_reference.info()->name());
  primary_clusters_.emplace(
      primary_cluster_reference.info()->name(),
      PrimaryClusterData{MessageUtil::hash(cluster), added_via_api, std::move(new_cluster)});

  cm_stats_.total_clusters_.set(primary_clusters_.size());
  if (num_erased) {
    cm_stats_.cluster_modified_.inc();
  } else {
    cm_stats_.cluster_added_.inc();
  }
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
                                           LoadBalancerContext* context) {
  ThreadLocalClusterManagerImpl& cluster_manager = tls_->getTyped<ThreadLocalClusterManagerImpl>();

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    return nullptr;
  }

  // Select a host and create a connection pool for it if it does not already exist.
  return entry->second->connPool(priority, context);
}

void ClusterManagerImpl::postThreadLocalClusterUpdate(
    const Cluster& primary_cluster, const std::vector<HostSharedPtr>& hosts_added,
    const std::vector<HostSharedPtr>& hosts_removed) {
  if (init_helper_.state() == ClusterManagerInitHelper::State::Loading) {
    // A cluster may try to post updates before we are ready for multi-threading. Block this case
    // since we will post the update in postInitializeCluster().
    return;
  }

  HostVectorConstSharedPtr hosts_copy(new std::vector<HostSharedPtr>(primary_cluster.hosts()));
  HostVectorConstSharedPtr healthy_hosts_copy(
      new std::vector<HostSharedPtr>(primary_cluster.healthyHosts()));
  HostListsConstSharedPtr hosts_per_locality_copy(
      new std::vector<std::vector<HostSharedPtr>>(primary_cluster.hostsPerLocality()));
  HostListsConstSharedPtr healthy_hosts_per_locality_copy(
      new std::vector<std::vector<HostSharedPtr>>(primary_cluster.healthyHostsPerLocality()));

  tls_->runOnAllThreads([
    this, name = primary_cluster.info()->name(), hosts_copy, healthy_hosts_copy,
    hosts_per_locality_copy, healthy_hosts_per_locality_copy, hosts_added, hosts_removed
  ]()
                            ->void {
                              ThreadLocalClusterManagerImpl::updateClusterMembership(
                                  name, hosts_copy, healthy_hosts_copy, hosts_per_locality_copy,
                                  healthy_hosts_per_locality_copy, hosts_added, hosts_removed,
                                  *tls_);
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
    return logical_host->createConnection(cluster_manager.thread_local_dispatcher_);
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

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ThreadLocalClusterManagerImpl(
    ClusterManagerImpl& parent, Event::Dispatcher& dispatcher,
    const Optional<std::string>& local_cluster_name)
    : parent_(parent), thread_local_dispatcher_(dispatcher) {
  // If local cluster is defined then we need to initialize it first.
  if (local_cluster_name.valid()) {
    ENVOY_LOG(debug, "adding TLS local cluster {}", local_cluster_name.value());
    auto& local_cluster = parent.primary_clusters_.at(local_cluster_name.value()).cluster_;
    thread_local_clusters_[local_cluster_name.value()].reset(
        new ClusterEntry(*this, local_cluster->info()));
  }

  local_host_set_ = local_cluster_name.valid()
                        ? &thread_local_clusters_[local_cluster_name.value()]->host_set_
                        : nullptr;

  for (auto& cluster : parent.primary_clusters_) {
    // If local cluster name is set then we already initialized this cluster.
    if (local_cluster_name.valid() && local_cluster_name.value() == cluster.first) {
      continue;
    }

    ENVOY_LOG(debug, "adding TLS initial cluster {}", cluster.first);
    ASSERT(thread_local_clusters_.count(cluster.first) == 0);
    thread_local_clusters_[cluster.first].reset(
        new ClusterEntry(*this, cluster.second.cluster_->info()));
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::~ThreadLocalClusterManagerImpl() {
  // Clear out connection pools as well as the thread local cluster map so that we release all
  // primary cluster pointers. Currently we have to free all non-local clusters before we free
  // the local cluster. This is because non-local clusters have a member update callback registered
  // with the local cluster.
  // TODO(mattklein123): The above is sub-optimal and is related to the TODO in
  //                     redis/conn_pool_impl.cc. Will fix at the same time.
  ENVOY_LOG(debug, "shutting down thread local cluster manager");
  host_http_conn_pool_map_.clear();
  for (auto& cluster : thread_local_clusters_) {
    if (&cluster.second->host_set_ != local_host_set_) {
      cluster.second.reset();
    }
  }
  thread_local_clusters_.clear();
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    const std::vector<HostSharedPtr>& hosts) {
  for (const HostSharedPtr& host : hosts) {
    auto container = host_http_conn_pool_map_.find(host);
    if (container != host_http_conn_pool_map_.end()) {
      drainConnPools(host, container->second);
    }
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    HostSharedPtr old_host, ConnPoolsContainer& container) {
  for (const Http::ConnectionPool::InstancePtr& pool : container.pools_) {
    if (pool) {
      container.drains_remaining_++;
    }
  }

  for (const Http::ConnectionPool::InstancePtr& pool : container.pools_) {
    if (!pool) {
      continue;
    }

    pool->addDrainedCallback([this, old_host]() -> void {
      ConnPoolsContainer& container = host_http_conn_pool_map_[old_host];
      ASSERT(container.drains_remaining_ > 0);
      container.drains_remaining_--;
      if (container.drains_remaining_ == 0) {
        for (Http::ConnectionPool::InstancePtr& pool : container.pools_) {
          thread_local_dispatcher_.deferredDelete(std::move(pool));
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
    const std::string& name, HostVectorConstSharedPtr hosts, HostVectorConstSharedPtr healthy_hosts,
    HostListsConstSharedPtr hosts_per_locality, HostListsConstSharedPtr healthy_hosts_per_locality,
    const std::vector<HostSharedPtr>& hosts_added, const std::vector<HostSharedPtr>& hosts_removed,
    ThreadLocal::Slot& tls) {

  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  config.thread_local_clusters_[name]->host_set_.updateHosts(
      std::move(hosts), std::move(healthy_hosts), std::move(hosts_per_locality),
      std::move(healthy_hosts_per_locality), hosts_added, hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::onHostHealthFailure(
    const HostSharedPtr& host, ThreadLocal::Slot& tls) {

  // Close all HTTP connection pool connections in the case of a host health failure. If outlier/
  // health is due to ECMP flow hashing issues for example, a new set of connections might do
  // better.
  // TODO(mattklein123): This function is currently very specific, but in the future when we do
  // more granular host set changes, we should be able to capture single host changes and make them
  // more targeted.
  ThreadLocalClusterManagerImpl& config = tls.getTyped<ThreadLocalClusterManagerImpl>();
  const auto& container = config.host_http_conn_pool_map_.find(host);
  if (container != config.host_http_conn_pool_map_.end()) {
    for (const Http::ConnectionPool::InstancePtr& pool : container->second.pools_) {
      if (pool == nullptr) {
        continue;
      }

      pool->closeConnections();
    }
  }
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    ThreadLocalClusterManagerImpl& parent, ClusterInfoConstSharedPtr cluster)
    : parent_(parent), cluster_info_(cluster),
      http_async_client_(*cluster, parent.parent_.stats_, parent.thread_local_dispatcher_,
                         parent.parent_.local_info_, parent.parent_, parent.parent_.runtime_,
                         parent.parent_.random_,
                         Router::ShadowWriterPtr{new Router::ShadowWriterImpl(parent.parent_)}) {
  if (cluster->lbSubsetInfo().isEnabled()) {
    lb_.reset(new SubsetLoadBalancer(cluster->lbType(), host_set_, parent.local_host_set_,
                                     cluster->stats(), parent.parent_.runtime_,
                                     parent.parent_.random_, cluster->lbSubsetInfo(),
                                     cluster->lbRingHashConfig()));
  } else {
    switch (cluster->lbType()) {
    case LoadBalancerType::LeastRequest: {
      lb_.reset(new LeastRequestLoadBalancer(host_set_, parent.local_host_set_, cluster->stats(),
                                             parent.parent_.runtime_, parent.parent_.random_));
      break;
    }
    case LoadBalancerType::Random: {
      lb_.reset(new RandomLoadBalancer(host_set_, parent.local_host_set_, cluster->stats(),
                                       parent.parent_.runtime_, parent.parent_.random_));
      break;
    }
    case LoadBalancerType::RoundRobin: {
      lb_.reset(new RoundRobinLoadBalancer(host_set_, parent.local_host_set_, cluster->stats(),
                                           parent.parent_.runtime_, parent.parent_.random_));
      break;
    }
    case LoadBalancerType::RingHash: {
      lb_.reset(new RingHashLoadBalancer(host_set_, cluster->stats(), parent.parent_.runtime_,
                                         parent.parent_.random_, cluster->lbRingHashConfig()));
      break;
    }
    case LoadBalancerType::OriginalDst: {
      lb_.reset(new OriginalDstCluster::LoadBalancer(
          host_set_, parent.parent_.primary_clusters_.at(cluster->name()).cluster_));
      break;
    }
    }
  }

  host_set_.addMemberUpdateCb([this](const std::vector<HostSharedPtr>&,
                                     const std::vector<HostSharedPtr>& hosts_removed) -> void {
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
  parent_.drainConnPools(host_set_.hosts());
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::connPool(
    ResourcePriority priority, LoadBalancerContext* context) {
  HostConstSharedPtr host = lb_->chooseHost(context);
  if (!host) {
    ENVOY_LOG(debug, "no healthy host for HTTP connection pool");
    cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return nullptr;
  }

  ConnPoolsContainer& container = parent_.host_http_conn_pool_map_[host];
  ASSERT(enumToInt(priority) < container.pools_.size());
  if (!container.pools_[enumToInt(priority)]) {
    container.pools_[enumToInt(priority)] =
        parent_.parent_.factory_.allocateConnPool(parent_.thread_local_dispatcher_, host, priority);
  }

  return container.pools_[enumToInt(priority)].get();
}

ClusterManagerPtr ProdClusterManagerFactory::clusterManagerFromProto(
    const envoy::api::v2::Bootstrap& bootstrap, Stats::Store& stats, ThreadLocal::Instance& tls,
    Runtime::Loader& runtime, Runtime::RandomGenerator& random,
    const LocalInfo::LocalInfo& local_info, AccessLog::AccessLogManager& log_manager) {
  return ClusterManagerPtr{new ClusterManagerImpl(bootstrap, *this, stats, tls, runtime, random,
                                                  local_info, log_manager, primary_dispatcher_)};
}

Http::ConnectionPool::InstancePtr
ProdClusterManagerFactory::allocateConnPool(Event::Dispatcher& dispatcher, HostConstSharedPtr host,
                                            ResourcePriority priority) {
  if ((host->cluster().features() & ClusterInfo::Features::HTTP2) &&
      runtime_.snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http2::ProdConnPoolImpl(dispatcher, host, priority)};
  } else {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http1::ConnPoolImplProd(dispatcher, host, priority)};
  }
}

ClusterSharedPtr ProdClusterManagerFactory::clusterFromProto(
    const envoy::api::v2::Cluster& cluster, ClusterManager& cm,
    Outlier::EventLoggerSharedPtr outlier_event_logger, bool added_via_api) {
  return ClusterImplBase::create(cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_,
                                 runtime_, random_, primary_dispatcher_, local_info_,
                                 outlier_event_logger, added_via_api);
}

CdsApiPtr
ProdClusterManagerFactory::createCds(const envoy::api::v2::ConfigSource& cds_config,
                                     const Optional<envoy::api::v2::ConfigSource>& eds_config,
                                     ClusterManager& cm) {
  return CdsApiImpl::create(cds_config, eds_config, cm, primary_dispatcher_, random_, local_info_,
                            stats_);
}

} // namespace Upstream
} // namespace Envoy
