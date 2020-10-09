#include "extensions/clusters/aggregate/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::aggregate::v3::ClusterConfig& config,
                 Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
                 Random::RandomGenerator& random,
                 Server::Configuration::TransportSocketFactoryContextImpl& factory_context,
                 Stats::ScopePtr&& stats_scope, ThreadLocal::SlotAllocator& tls, bool added_via_api)
    : Upstream::ClusterImplBase(cluster, runtime, factory_context, std::move(stats_scope),
                                added_via_api),
      cluster_manager_(cluster_manager), runtime_(runtime), random_(random),
      tls_(tls.allocateSlot()), clusters_(config.clusters().begin(), config.clusters().end()) {}

PriorityContextPtr
Cluster::linearizePrioritySet(const std::function<bool(const std::string&)>& skip_predicate) {
  PriorityContextPtr priority_context = std::make_unique<PriorityContext>();
  uint32_t next_priority_after_linearizing = 0;

  // Linearize the priority set. e.g. for clusters [C_0, C_1, C_2] referred in aggregate cluster
  //    C_0 [P_0, P_1, P_2]
  //    C_1 [P_0, P_1]
  //    C_2 [P_0, P_1, P_2, P_3]
  // The linearization result is:
  //    [C_0.P_0, C_0.P_1, C_0.P_2, C_1.P_0, C_1.P_1, C_2.P_0, C_2.P_1, C_2.P_2, C_2.P_3]
  // and the traffic will be distributed among these priorities.
  for (const auto& cluster : clusters_) {
    if (skip_predicate(cluster)) {
      continue;
    }
    auto tlc = cluster_manager_.get(cluster);
    // It is possible that the cluster doesn't exist, e.g., the cluster cloud be deleted or the
    // cluster hasn't been added by xDS.
    if (tlc == nullptr) {
      continue;
    }

    uint32_t priority_in_current_cluster = 0;
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        priority_context->priority_set_.updateHosts(
            next_priority_after_linearizing, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, host_set->overprovisioningFactor());
        priority_context->priority_to_cluster_.emplace_back(
            std::make_pair(priority_in_current_cluster, tlc));

        priority_context->cluster_and_priority_to_linearized_priority_[std::make_pair(
            cluster, priority_in_current_cluster)] = next_priority_after_linearizing;
        next_priority_after_linearizing++;
      }
      priority_in_current_cluster++;
    }
  }

  return priority_context;
}

void Cluster::startPreInit() {
  for (const auto& cluster : clusters_) {
    auto tlc = cluster_manager_.get(cluster);
    // It is possible when initializing the cluster, the included cluster doesn't exist. e.g., the
    // cluster could be added dynamically by xDS.
    if (tlc == nullptr) {
      continue;
    }

    // Add callback for clusters initialized before aggregate cluster.
    tlc->prioritySet().addMemberUpdateCb(
        [this, cluster](const Upstream::HostVector&, const Upstream::HostVector&) {
          ENVOY_LOG(debug, "member update for cluster '{}' in aggregate cluster '{}'", cluster,
                    this->info()->name());
          refresh();
        });
  }
  refresh();
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);

  onPreInitComplete();
}

void Cluster::refresh(const std::function<bool(const std::string&)>& skip_predicate) {
  // Post the priority set to worker threads.
  // TODO(mattklein123): Remove "this" capture.
  tls_->runOnAllThreads([this, skip_predicate, cluster_name = this->info()->name()](
                            ThreadLocal::ThreadLocalObjectSharedPtr object)
                            -> ThreadLocal::ThreadLocalObjectSharedPtr {
    PriorityContextPtr priority_context = linearizePrioritySet(skip_predicate);
    Upstream::ThreadLocalCluster* cluster = cluster_manager_.get(cluster_name);
    ASSERT(cluster != nullptr);
    dynamic_cast<AggregateClusterLoadBalancer&>(cluster->loadBalancer())
        .refresh(std::move(priority_context));
    return object;
  });
}

void Cluster::onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) {
  if (std::find(clusters_.begin(), clusters_.end(), cluster.info()->name()) != clusters_.end()) {
    ENVOY_LOG(debug, "adding or updating cluster '{}' for aggregate cluster '{}'",
              cluster.info()->name(), info()->name());
    refresh();
    cluster.prioritySet().addMemberUpdateCb(
        [this](const Upstream::HostVector&, const Upstream::HostVector&) { refresh(); });
  }
}

void Cluster::onClusterRemoval(const std::string& cluster_name) {
  //  The onClusterRemoval callback is called before the thread local cluster is removed. There
  //  will be a dangling pointer to the thread local cluster if the deleted cluster is not skipped
  //  when we refresh the load balancer.
  if (std::find(clusters_.begin(), clusters_.end(), cluster_name) != clusters_.end()) {
    ENVOY_LOG(debug, "removing cluster '{}' from aggreagte cluster '{}'", cluster_name,
              info()->name());
    refresh([cluster_name](const std::string& c) { return cluster_name == c; });
  }
}

absl::optional<uint32_t> AggregateClusterLoadBalancer::LoadBalancerImpl::hostToLinearizedPriority(
    const Upstream::HostDescription& host) const {
  auto it = priority_context_.cluster_and_priority_to_linearized_priority_.find(
      std::make_pair(host.cluster().name(), host.priority()));

  if (it != priority_context_.cluster_and_priority_to_linearized_priority_.end()) {
    return it->second;
  } else {
    // The HostSet can change due to CDS/EDS updates between retries.
    return absl::nullopt;
  }
}

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::LoadBalancerImpl::chooseHost(Upstream::LoadBalancerContext* context) {
  const Upstream::HealthyAndDegradedLoad* priority_loads = nullptr;
  if (context != nullptr) {
    priority_loads = &context->determinePriorityLoad(
        priority_set_, per_priority_load_,
        [this](const auto& host) { return hostToLinearizedPriority(host); });
  } else {
    priority_loads = &per_priority_load_;
  }

  const auto priority_pair =
      choosePriority(random_.random(), priority_loads->healthy_priority_load_,
                     priority_loads->degraded_priority_load_);

  AggregateLoadBalancerContext aggregate_context(
      context, priority_pair.second,
      priority_context_.priority_to_cluster_[priority_pair.first].first);

  Upstream::ThreadLocalCluster* cluster =
      priority_context_.priority_to_cluster_[priority_pair.first].second;
  return cluster->loadBalancer().chooseHost(&aggregate_context);
}

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (load_balancer_) {
    return load_balancer_->chooseHost(context);
  }
  return nullptr;
}

std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::aggregate::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context,
    Server::Configuration::TransportSocketFactoryContextImpl& socket_factory_context,
    Stats::ScopePtr&& stats_scope) {
  auto new_cluster =
      std::make_shared<Cluster>(cluster, proto_config, context.clusterManager(), context.runtime(),
                                context.api().randomGenerator(), socket_factory_context,
                                std::move(stats_scope), context.tls(), context.addedViaApi());
  auto lb = std::make_unique<AggregateThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
