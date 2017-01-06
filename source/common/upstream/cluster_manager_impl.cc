#include "cluster_manager_impl.h"
#include "load_balancer_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/network/dns.h"
#include "envoy/runtime/runtime.h"

#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/http1/conn_pool.h"
#include "common/http/http2/conn_pool.h"
#include "common/http/async_client_impl.h"
#include "common/router/shadow_writer_impl.h"

namespace Upstream {

ClusterManagerImpl::ClusterManagerImpl(const Json::Object& config, ClusterManagerFactory& factory,
                                       Stats::Store& stats, ThreadLocal::Instance& tls,
                                       Runtime::Loader& runtime, Runtime::RandomGenerator& random,
                                       const LocalInfo::LocalInfo& local_info,
                                       AccessLog::AccessLogManager& log_manager)
    : factory_(factory), runtime_(runtime), stats_(stats), tls_(tls), random_(random),
      thread_local_slot_(tls.allocateSlot()), local_info_(local_info) {

  std::vector<Json::ObjectPtr> clusters = config.getObjectArray("clusters");
  pending_cluster_init_ = clusters.size();

  if (config.hasObject("outlier_detection")) {
    std::string event_log_file_path =
        config.getObject("outlier_detection")->getString("event_log_path", "");
    if (!event_log_file_path.empty()) {
      outlier_event_logger_.reset(new Outlier::EventLoggerImpl(log_manager, event_log_file_path,
                                                               ProdSystemTimeSource::instance_));
    }
  }

  if (config.hasObject("sds")) {
    pending_cluster_init_++;
    loadCluster(*config.getObject("sds")->getObject("cluster"), false);

    SdsConfig sds_config{
        config.getObject("sds")->getObject("cluster")->getString("name"),
        std::chrono::milliseconds(config.getObject("sds")->getInteger("refresh_delay_ms"))};

    sds_config_.value(sds_config);
  }

  for (const Json::ObjectPtr& cluster : clusters) {
    loadCluster(*cluster, false);
  }

  Optional<std::string> local_cluster_name;
  if (config.hasObject("local_cluster_name")) {
    local_cluster_name.value(config.getString("local_cluster_name"));
    if (primary_clusters_.find(local_cluster_name.value()) == primary_clusters_.end()) {
      throw EnvoyException(
          fmt::format("local cluster '{}' must be defined", local_cluster_name.value()));
    }
  }

  tls.set(thread_local_slot_,
          [this, local_cluster_name](Event::Dispatcher& dispatcher)
              -> ThreadLocal::ThreadLocalObjectPtr {
                return ThreadLocal::ThreadLocalObjectPtr{
                    new ThreadLocalClusterManagerImpl(*this, dispatcher, local_cluster_name)};
              });

  // To avoid threading issues, for those clusters that start with hosts already in them (like
  // the static cluster), we need to post an update onto each thread to notify them of the update.
  for (auto& cluster : primary_clusters_) {
    postInitializeCluster(*cluster.second.cluster_);
  }
}

void ClusterManagerImpl::postInitializeCluster(Cluster& cluster) {
  if (cluster.hosts().empty()) {
    return;
  }

  postThreadLocalClusterUpdate(cluster, cluster.hosts(), std::vector<HostPtr>{});
}

bool ClusterManagerImpl::addOrUpdatePrimaryCluster(const Json::Object& new_config) {
  // First we need to see if this new config is new or an update to an existing dynamic cluster.
  // We don't allow updates to statically configured clusters in the main configuration.
  // TODO: Stats
  std::string cluster_name = new_config.getString("name");
  auto existing_cluster = primary_clusters_.find(cluster_name);
  if (existing_cluster != primary_clusters_.end() &&
      (!existing_cluster->second.added_via_api_ ||
       existing_cluster->second.config_hash_ == new_config.hash())) {
    return false;
  }

  loadCluster(new_config, true);
  ClusterInfoPtr new_cluster = primary_clusters_.at(cluster_name).cluster_->info();
  tls_.runOnAllThreads([this, new_cluster]() -> void {
    ThreadLocalClusterManagerImpl& cluster_manager =
        tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

    cluster_manager.thread_local_clusters_[new_cluster->name()].reset(
        new ThreadLocalClusterManagerImpl::ClusterEntry(cluster_manager, new_cluster));
  });

  postInitializeCluster(*primary_clusters_.at(cluster_name).cluster_);
  return true;
}

bool ClusterManagerImpl::removePrimaryCluster(const std::string& cluster_name) {
  // TODO: Stats
  auto existing_cluster = primary_clusters_.find(cluster_name);
  if (existing_cluster == primary_clusters_.end() || !existing_cluster->second.added_via_api_) {
    return false;
  }

  primary_clusters_.erase(cluster_name);
  tls_.runOnAllThreads([this, cluster_name]() -> void {
    ThreadLocalClusterManagerImpl& cluster_manager =
        tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

    cluster_manager.thread_local_clusters_.erase(cluster_name);
  });

  return true;
}

void ClusterManagerImpl::loadCluster(const Json::Object& cluster, bool added_via_api) {
  ClusterPtr new_cluster =
      factory_.clusterFromJson(cluster, *this, sds_config_, outlier_event_logger_);

  if (!added_via_api) {
    if (primary_clusters_.find(new_cluster->info()->name()) != primary_clusters_.end()) {
      throw EnvoyException(
          fmt::format("cluster manager: duplicate cluster '{}'", new_cluster->info()->name()));
    }

    if (new_cluster->initializePhase() == Cluster::InitializePhase::Primary) {
      new_cluster->initialize();
    } else {
      ASSERT(new_cluster->initializePhase() == Cluster::InitializePhase::Secondary);
      secondary_init_clusters_.push_back(new_cluster.get());
    }

    ASSERT(pending_cluster_init_ > 0);
    new_cluster->setInitializedCb([this]() -> void {
      ASSERT(pending_cluster_init_ > 0);
      if (--pending_cluster_init_ == 0) {
        if (initialized_callback_) {
          initialized_callback_();
        }
      } else if (pending_cluster_init_ == secondary_init_clusters_.size()) {
        // All primary clusters have initialized. Now we start up the secondary clusters.
        for (Cluster* cluster : secondary_init_clusters_) {
          cluster->initialize();
        }
      }
    });
  } else {
    new_cluster->initialize();
  }

  const Cluster& primary_cluster_reference = *new_cluster;
  new_cluster->addMemberUpdateCb([&primary_cluster_reference, this](
      const std::vector<HostPtr>& hosts_added, const std::vector<HostPtr>& hosts_removed) {
    // This fires when a cluster is about to have an updated member set. We need to send this
    // out to all of the thread local configurations.
    postThreadLocalClusterUpdate(primary_cluster_reference, hosts_added, hosts_removed);
  });

  // emplace() will do nothing if the key already exists. Always erase first.
  primary_clusters_.erase(primary_cluster_reference.info()->name());
  primary_clusters_.emplace(
      primary_cluster_reference.info()->name(),
      PrimaryClusterData{cluster.hash(), added_via_api, std::move(new_cluster)});
}

ClusterInfoPtr ClusterManagerImpl::get(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry != cluster_manager.thread_local_clusters_.end()) {
    return entry->second->cluster_info_;
  } else {
    return nullptr;
  }
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::httpConnPoolForCluster(const std::string& cluster, ResourcePriority priority) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

  // Select a host and create a connection pool for it if it does not already exist.
  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    throw EnvoyException(fmt::format("unknown cluster '{}'", cluster));
  }

  return entry->second->connPool(priority);
}

void ClusterManagerImpl::postThreadLocalClusterUpdate(const Cluster& primary_cluster,
                                                      const std::vector<HostPtr>& hosts_added,
                                                      const std::vector<HostPtr>& hosts_removed) {
  const std::string& name = primary_cluster.info()->name();
  ConstHostVectorPtr hosts_copy(new std::vector<HostPtr>(primary_cluster.hosts()));
  ConstHostVectorPtr healthy_hosts_copy(new std::vector<HostPtr>(primary_cluster.healthyHosts()));
  ConstHostListsPtr hosts_per_zone_copy(
      new std::vector<std::vector<HostPtr>>(primary_cluster.hostsPerZone()));
  ConstHostListsPtr healthy_hosts_per_zone_copy(
      new std::vector<std::vector<HostPtr>>(primary_cluster.healthyHostsPerZone()));

  tls_.runOnAllThreads([this, name, hosts_copy, healthy_hosts_copy, hosts_per_zone_copy,
                        healthy_hosts_per_zone_copy, hosts_added, hosts_removed]() -> void {
    ThreadLocalClusterManagerImpl::updateClusterMembership(
        name, hosts_copy, healthy_hosts_copy, hosts_per_zone_copy, healthy_hosts_per_zone_copy,
        hosts_added, hosts_removed, tls_, thread_local_slot_);
  });
}

Host::CreateConnectionData ClusterManagerImpl::tcpConnForCluster(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);

  auto entry = cluster_manager.thread_local_clusters_.find(cluster);
  if (entry == cluster_manager.thread_local_clusters_.end()) {
    throw EnvoyException(fmt::format("unknown cluster '{}'", cluster));
  }

  ConstHostPtr logical_host = entry->second->lb_->chooseHost();
  if (logical_host) {
    return logical_host->createConnection(cluster_manager.thread_local_dispatcher_);
  } else {
    entry->second->cluster_info_->stats().upstream_cx_none_healthy_.inc();
    return {nullptr, nullptr};
  }
}

Http::AsyncClient& ClusterManagerImpl::httpAsyncClientForCluster(const std::string& cluster) {
  ThreadLocalClusterManagerImpl& cluster_manager =
      tls_.getTyped<ThreadLocalClusterManagerImpl>(thread_local_slot_);
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

    thread_local_clusters_[cluster.first].reset(
        new ClusterEntry(*this, cluster.second.cluster_->info()));
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::drainConnPools(
    HostPtr old_host, ConnPoolsContainer& container) {
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
  }
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::updateClusterMembership(
    const std::string& name, ConstHostVectorPtr hosts, ConstHostVectorPtr healthy_hosts,
    ConstHostListsPtr hosts_per_zone, ConstHostListsPtr healthy_hosts_per_zone,
    const std::vector<HostPtr>& hosts_added, const std::vector<HostPtr>& hosts_removed,
    ThreadLocal::Instance& tls, uint32_t thead_local_slot) {

  ThreadLocalClusterManagerImpl& config =
      tls.getTyped<ThreadLocalClusterManagerImpl>(thead_local_slot);

  ASSERT(config.thread_local_clusters_.find(name) != config.thread_local_clusters_.end());
  config.thread_local_clusters_[name]->host_set_.updateHosts(
      hosts, healthy_hosts, hosts_per_zone, healthy_hosts_per_zone, hosts_added, hosts_removed);
}

void ClusterManagerImpl::ThreadLocalClusterManagerImpl::shutdown() {
  // Clear out connection pools as well as the thread local cluster map so that we release all
  // primary cluster pointers.
  host_http_conn_pool_map_.clear();
  thread_local_clusters_.clear();
}

ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::ClusterEntry(
    ThreadLocalClusterManagerImpl& parent, ClusterInfoPtr cluster)
    : parent_(parent), cluster_info_(cluster),
      http_async_client_(*cluster, parent.parent_.stats_, parent.thread_local_dispatcher_,
                         parent.parent_.local_info_, parent.parent_, parent.parent_.runtime_,
                         parent.parent_.random_,
                         Router::ShadowWriterPtr{new Router::ShadowWriterImpl(parent.parent_)}) {

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
  }

  host_set_.addMemberUpdateCb(
      [this](const std::vector<HostPtr>&, const std::vector<HostPtr>& hosts_removed) -> void {
        // We need to go through and purge any connection pools for hosts that got deleted.
        // Even if two hosts actually point to the same address this will be safe, since if a
        // host is readded it will be a different physical HostPtr.
        for (const HostPtr& old_host : hosts_removed) {
          auto container = parent_.host_http_conn_pool_map_.find(old_host);
          if (container != parent_.host_http_conn_pool_map_.end()) {
            parent_.drainConnPools(old_host, container->second);
          }
        }
      });
}

Http::ConnectionPool::Instance*
ClusterManagerImpl::ThreadLocalClusterManagerImpl::ClusterEntry::connPool(
    ResourcePriority priority) {
  ConstHostPtr host = lb_->chooseHost();
  if (!host) {
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

Http::ConnectionPool::InstancePtr
ProdClusterManagerFactory::allocateConnPool(Event::Dispatcher& dispatcher, ConstHostPtr host,
                                            ResourcePriority priority) {
  if ((host->cluster().features() & ClusterInfo::Features::HTTP2) &&
      runtime_.snapshot().featureEnabled("upstream.use_http2", 100)) {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http2::ProdConnPoolImpl(dispatcher, host, stats_, priority)};
  } else {
    return Http::ConnectionPool::InstancePtr{
        new Http::Http1::ConnPoolImplProd(dispatcher, host, stats_, priority)};
  }
}

ClusterPtr
ProdClusterManagerFactory::clusterFromJson(const Json::Object& cluster, ClusterManager& cm,
                                           const Optional<SdsConfig>& sds_config,
                                           Outlier::EventLoggerPtr outlier_event_logger) {
  return ClusterImplBase::create(cluster, cm, stats_, tls_, dns_resolver_, ssl_context_manager_,
                                 runtime_, random_, primary_dispatcher_, sds_config, local_info_,
                                 outlier_event_logger);
}

} // Upstream
