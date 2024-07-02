#include "source/extensions/clusters/aggregate/cluster.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.h"
#include "envoy/extensions/clusters/aggregate/v3/cluster.pb.validate.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Aggregate {

Cluster::Cluster(const envoy::config::cluster::v3::Cluster& cluster,
                 const envoy::extensions::clusters::aggregate::v3::ClusterConfig& config,
                 Upstream::ClusterFactoryContext& context, absl::Status& creation_status)
    : Upstream::ClusterImplBase(cluster, context, creation_status),
      cluster_manager_(context.clusterManager()),
      runtime_(context.serverFactoryContext().runtime()),
      random_(context.serverFactoryContext().api().randomGenerator()),
      clusters_(std::make_shared<ClusterSet>(config.clusters().begin(), config.clusters().end())) {}

AggregateClusterLoadBalancer::AggregateClusterLoadBalancer(
    const Upstream::ClusterInfoConstSharedPtr& parent_info,
    Upstream::ClusterManager& cluster_manager, Runtime::Loader& runtime,
    Random::RandomGenerator& random, const ClusterSetConstSharedPtr& clusters)
    : parent_info_(parent_info), cluster_manager_(cluster_manager), runtime_(runtime),
      random_(random), clusters_(clusters) {
  for (const auto& cluster : *clusters_) {
    auto tlc = cluster_manager_.getThreadLocalCluster(cluster);
    // It is possible when initializing the cluster, the included cluster doesn't exist. e.g., the
    // cluster could be added dynamically by xDS.
    if (tlc == nullptr) {
      continue;
    }

    // Add callback for clusters initialized before aggregate cluster.
    addMemberUpdateCallbackForCluster(*tlc);
  }
  refresh();
  handle_ = cluster_manager_.addThreadLocalClusterUpdateCallbacks(*this);
}

void AggregateClusterLoadBalancer::addMemberUpdateCallbackForCluster(
    Upstream::ThreadLocalCluster& thread_local_cluster) {
  member_update_cbs_[thread_local_cluster.info()->name()] =
      thread_local_cluster.prioritySet().addMemberUpdateCb(
          [this, target_cluster_info = thread_local_cluster.info()](const Upstream::HostVector&,
                                                                    const Upstream::HostVector&) {
            ENVOY_LOG(debug, "member update for cluster '{}' in aggregate cluster '{}'",
                      target_cluster_info->name(), parent_info_->name());
            refresh();
            return absl::OkStatus();
          });
}

PriorityContextPtr
AggregateClusterLoadBalancer::linearizePrioritySet(OptRef<const std::string> excluded_cluster) {
  PriorityContextPtr priority_context = std::make_unique<PriorityContext>();
  uint32_t next_priority_after_linearizing = 0;

  // Linearize the priority set. e.g. for clusters [C_0, C_1, C_2] referred in aggregate cluster
  //    C_0 [P_0, P_1, P_2]
  //    C_1 [P_0, P_1]
  //    C_2 [P_0, P_1, P_2, P_3]
  // The linearization result is:
  //    [C_0.P_0, C_0.P_1, C_0.P_2, C_1.P_0, C_1.P_1, C_2.P_0, C_2.P_1, C_2.P_2, C_2.P_3]
  // and the traffic will be distributed among these priorities.
  for (const auto& cluster : *clusters_) {
    if (excluded_cluster.has_value() && excluded_cluster.value().get() == cluster) {
      continue;
    }
    auto tlc = cluster_manager_.getThreadLocalCluster(cluster);
    // It is possible that the cluster doesn't exist, e.g., the cluster could be deleted or the
    // cluster hasn't been added by xDS.
    if (tlc == nullptr) {
      ENVOY_LOG(debug, "refresh: cluster '{}' absent in aggregate cluster '{}'", cluster,
                parent_info_->name());
      continue;
    } else {
      ENVOY_LOG(debug, "refresh: cluster '{}' found in aggregate cluster '{}'", cluster,
                parent_info_->name());
    }

    uint32_t priority_in_current_cluster = 0;
    for (const auto& host_set : tlc->prioritySet().hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        priority_context->priority_set_.updateHosts(
            next_priority_after_linearizing, Upstream::HostSetImpl::updateHostsParams(*host_set),
            host_set->localityWeights(), host_set->hosts(), {}, random_.random(),
            host_set->weightedPriorityHealth(), host_set->overprovisioningFactor());
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

void AggregateClusterLoadBalancer::refresh(OptRef<const std::string> excluded_cluster) {
  PriorityContextPtr priority_context = linearizePrioritySet(excluded_cluster);
  if (!priority_context->priority_set_.hostSetsPerPriority().empty()) {
    load_balancer_ = std::make_unique<LoadBalancerImpl>(
        *priority_context, parent_info_->lbStats(), runtime_, random_, parent_info_->lbConfig());
  } else {
    load_balancer_ = nullptr;
  }
  priority_context_ = std::move(priority_context);
}

void AggregateClusterLoadBalancer::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand& get_cluster) {
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "adding or updating cluster '{}' for aggregate cluster '{}'", cluster_name,
              parent_info_->name());
    auto& cluster = get_cluster();
    refresh();
    addMemberUpdateCallbackForCluster(cluster);
  }
}

void AggregateClusterLoadBalancer::onClusterRemoval(const std::string& cluster_name) {
  //  The onClusterRemoval callback is called before the thread local cluster is removed. There
  //  will be a dangling pointer to the thread local cluster if the deleted cluster is not skipped
  //  when we refresh the load balancer.
  if (std::find(clusters_->begin(), clusters_->end(), cluster_name) != clusters_->end()) {
    ENVOY_LOG(debug, "removing cluster '{}' from aggregate cluster '{}'", cluster_name,
              parent_info_->name());
    refresh(cluster_name);
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

Upstream::HostConstSharedPtr
AggregateClusterLoadBalancer::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  if (load_balancer_) {
    return load_balancer_->peekAnotherHost(context);
  }
  return nullptr;
}

absl::optional<Upstream::SelectedPoolAndConnection>
AggregateClusterLoadBalancer::selectExistingConnection(Upstream::LoadBalancerContext* context,
                                                       const Upstream::Host& host,
                                                       std::vector<uint8_t>& hash_key) {
  if (load_balancer_) {
    return load_balancer_->selectExistingConnection(context, host, hash_key);
  }
  return absl::nullopt;
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
AggregateClusterLoadBalancer::lifetimeCallbacks() {
  if (load_balancer_) {
    return load_balancer_->lifetimeCallbacks();
  }
  return {};
}

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
ClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::aggregate::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto new_cluster =
      std::shared_ptr<Cluster>(new Cluster(cluster, proto_config, context, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  auto lb = std::make_unique<AggregateThreadAwareLoadBalancer>(*new_cluster);
  return std::make_pair(new_cluster, std::move(lb));
}

REGISTER_FACTORY(ClusterFactory, Upstream::ClusterFactory);

} // namespace Aggregate
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
