#include "common/upstream/shuffle_subset_lb.h"

#include <memory>
#include <algorithm>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"
#include "common/upstream/load_balancer_impl.h"
#include "common/upstream/maglev_lb.h"
#include "common/upstream/ring_hash_lb.h"

#include "absl/container/node_hash_set.h"
#include "quiche/quic/core/quic_lru_cache.h"

namespace Envoy {
namespace Upstream {

ShuffleSubsetLoadBalancer::ShuffleSubsetLoadBalancer(
    LoadBalancerType lb_type, PrioritySet& priority_set, const PrioritySet* local_priority_set,
    ClusterStats& stats, Stats::Scope& scope, Runtime::Loader& runtime,
    Random::RandomGenerator& random, const LoadBalancerShuffleSubsetInfo& shuffle,
    const absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig>&
        lb_ring_hash_config,
    const absl::optional<envoy::config::cluster::v3::Cluster::MaglevLbConfig>& lb_maglev_config,
    const absl::optional<envoy::config::cluster::v3::Cluster::LeastRequestLbConfig>&
        least_request_config,
    const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
    : lb_type_(lb_type), lb_ring_hash_config_(lb_ring_hash_config),
      lb_maglev_config_(lb_maglev_config), least_request_config_(least_request_config),
      common_config_(common_config), stats_(stats), scope_(scope), runtime_(runtime),
      random_(random), shard_size_(shuffle.shard_size()), cache_capacity_(shuffle.cache_capacity()), original_priority_set_(priority_set),
      original_local_priority_set_(local_priority_set)
   {
  ENVOY_LOG(info, "### ShuffleSubsetLoadBalancer::ShuffleSubsetLoadBalancer");
  ASSERT(shuffle.isEnabled());

  original_priority_set_callback_handle_ = priority_set.addPriorityUpdateCb(
      [this](uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed) {
        ENVOY_LOG(info, "### priority-update ("+std::to_string(priority)+")");
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          ENVOY_LOG(info, "### priority-update - DO ("+ std::to_string(hosts_added.size() + hosts_removed.size())+")");
          for (const auto &entry : cache_) {
            entry.second->updateSubset(priority, hosts_added, hosts_removed);
          }
        } else {
          ENVOY_LOG(info, "### priority-update - DONT");
          const auto& host_sets = original_priority_set_.hostSetsPerPriority();
          update(priority, host_sets[priority]->hosts(), {});
        }
      });
}

void ShuffleSubsetLoadBalancer::update(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed) {
  ENVOY_LOG(info, "### ShuffleSubsetLoadBalancer::update");
  for (const auto &entry : cache_) {
    entry.second->update(priority, hosts_added, hosts_removed);
  }
}

ShuffleSubsetLoadBalancer::~ShuffleSubsetLoadBalancer() {
  ENVOY_LOG(info, "### ShuffleSubsetLoadBalancer::~ShuffleSubsetLoadBalancer");
}

std::vector<uint32_t> * ShuffleSubsetLoadBalancer::combo(uint32_t i, uint32_t n, uint32_t k) {
    // https://stackoverflow.com/questions/1776442/nth-combination
    // This returns a list of elements for a given combinatoric index
    // e.g. combo(1, 3, 2) = [0, 2]
    ENVOY_LOG(info, "### ShuffleSubsetLoadBalancer::combo " + std::to_string(i) + " " + std::to_string(n) + " " + std::to_string(k) + " " );
    uint32_t counter = 1;
    uint32_t nCk = 1;
    for(uint32_t j = n; j > n-k; j--) {
        nCk *= j;
        nCk /= counter;
        counter++;
    } // O(n-k)
    uint32_t current = nCk;
    std::vector<uint32_t> * ret = new std::vector<uint32_t>();
    for(uint32_t j = k; j > 0; j--) {
        nCk *= j;
        nCk /= n;
        for(;current-nCk > i;) {
            current -= nCk;
            nCk *= n-j;
            nCk -= nCk % j;
            n -= 1;
            nCk /= n;
        }
        n-=1;
        ret->push_back(n);
    } // O(k log(n/k))
    return ret;
}

HostConstSharedPtr ShuffleSubsetLoadBalancer::chooseHost(LoadBalancerContext* context) {
  ENVOY_LOG(info, "### ShuffleSubsetLoadBalancer::chooseHost");
  // Create unique hash based on metadata
  uint32_t hash = 0;
  if (context){
    const auto metadata = context->metadataMatchCriteria();
    if (metadata){
      for (const auto& entry : metadata->metadataMatchCriteria()) {
        hash += entry->value().hash(); // Overflows are OK
      }
    }
  }

  uint32_t size = original_priority_set_.hostSetsPerPriority()[0]->hosts().size();
  uint32_t min_shard_size = std::min(size, shard_size_);
  if (num_hosts_ != size) {
    num_hosts_ = size;
    // Calculate the number of combinations
    num_indices_ = 1;
    for (uint32_t i = size; i > 1; i--) {
      if (i <= min_shard_size && i <= size - min_shard_size)
        num_indices_ /= i;
      else if ((i > min_shard_size) && (i > size - min_shard_size))
        num_indices_ *= i;
    }
  }

  uint32_t index = hash % num_indices_;
  HostConstSharedPtr host;
  auto it = cache_.find(index);
  if (it == cache_.end()) {
    stats_.lb_shuffle_cache_miss_.inc();
    auto setNums = combo(index, size, min_shard_size);

    PriorityShuffleSubsetImplPtr shuffle_shard;
    if (cache_.size() >= cache_capacity_) {
      shuffle_shard = cache_.front().second;
      shuffle_shard->set_ = setNums;
      shuffle_shard->updateSubset(0, original_priority_set_.hostSetsPerPriority()[0]->hosts(), {});
      cache_.pop_front();
    } else {
      stats_.lb_shuffle_created_.inc();
      shuffle_shard = std::make_shared<PriorityShuffleSubsetImpl>(*this, setNums);
    }
    host = shuffle_shard->lb_->chooseHost(context);
    cache_.emplace(index, shuffle_shard);

  } else {
    stats_.lb_shuffle_cache_hit_.inc();
    auto value = it->second;
    cache_.erase(it);
    auto result = cache_.emplace(index, value);
    host = result.first->second.get()->lb_->chooseHost(context);
  }
  return host;
}

// Initialize a new HostSubsetImpl and LoadBalancer from the ShuffleSubsetLoadBalancer, filtering hosts
// with the given predicate.
ShuffleSubsetLoadBalancer::PriorityShuffleSubsetImpl::PriorityShuffleSubsetImpl(const ShuffleSubsetLoadBalancer& shuffle_lb, std::vector<uint32_t> * set)
    : set_(set), original_priority_set_(shuffle_lb.original_priority_set_)
      {
  ENVOY_LOG(info, "### PriorityShuffleSubsetImpl::PriorityShuffleSubsetImpl");
  for (size_t i = 0; i < shuffle_lb.original_priority_set_.hostSetsPerPriority().size(); ++i) {
    update(i, shuffle_lb.original_priority_set_.hostSetsPerPriority()[i]->hosts(), {});
  }

  switch (shuffle_lb.lb_type_) {
  case LoadBalancerType::LeastRequest:
    lb_ = std::make_unique<LeastRequestLoadBalancer>(
        *this, shuffle_lb.original_local_priority_set_, shuffle_lb.stats_, shuffle_lb.runtime_,
        shuffle_lb.random_, shuffle_lb.common_config_, shuffle_lb.least_request_config_);
    break;

  case LoadBalancerType::Random:
    lb_ = std::make_unique<RandomLoadBalancer>(*this, shuffle_lb.original_local_priority_set_,
                                               shuffle_lb.stats_, shuffle_lb.runtime_,
                                               shuffle_lb.random_, shuffle_lb.common_config_);
    break;

  case LoadBalancerType::RoundRobin:
    lb_ = std::make_unique<RoundRobinLoadBalancer>(*this, shuffle_lb.original_local_priority_set_,
                                                   shuffle_lb.stats_, shuffle_lb.runtime_,
                                                   shuffle_lb.random_, shuffle_lb.common_config_);
    break;

  case LoadBalancerType::RingHash:
    // TODO(mattklein123): The ring hash LB is thread aware, but currently the subset LB is not.
    // We should make the subset LB thread aware since the calculations are costly, and then we
    // can also use a thread aware sub-LB properly. The following works fine but is not optimal.
    thread_aware_lb_ = std::make_unique<RingHashLoadBalancer>(
        *this, shuffle_lb.stats_, shuffle_lb.scope_, shuffle_lb.runtime_, shuffle_lb.random_,
        shuffle_lb.lb_ring_hash_config_, shuffle_lb.common_config_);
    thread_aware_lb_->initialize();
    lb_ = thread_aware_lb_->factory()->create();
    break;

  case LoadBalancerType::Maglev:
    // TODO(mattklein123): The Maglev LB is thread aware, but currently the subset LB is not.
    // We should make the subset LB thread aware since the calculations are costly, and then we
    // can also use a thread aware sub-LB properly. The following works fine but is not optimal.
    thread_aware_lb_ = std::make_unique<MaglevLoadBalancer>(
        *this, shuffle_lb.stats_, shuffle_lb.scope_, shuffle_lb.runtime_, shuffle_lb.random_,
        shuffle_lb.lb_maglev_config_, shuffle_lb.common_config_);
    thread_aware_lb_->initialize();
    lb_ = thread_aware_lb_->factory()->create();
    break;

  case LoadBalancerType::OriginalDst:
  case LoadBalancerType::ClusterProvided:
    // LoadBalancerType::OriginalDst is blocked in the factory. LoadBalancerType::ClusterProvided
    // is impossible because the subset LB returns a null load balancer from its factory.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  triggerCallbacks();
}

void ShuffleSubsetLoadBalancer::HostSubsetImpl::update(const HostVector& hosts_added,
                                                const HostVector& hosts_removed,
                                                std::function<bool(const Host&)> predicate) {
  // We cache the result of matching the host against the predicate. This ensures
  // that we maintain a consistent view of the metadata and saves on computation
  // since metadata lookups can be expensive.
  //
  // We use an unordered container because this can potentially be in the tens of thousands.
  absl::node_hash_set<const Host*> matching_hosts;

  auto cached_predicate = [&matching_hosts](const auto& host) {
    return matching_hosts.count(&host) == 1;
  };


  // TODO(snowp): If we had a unhealthyHosts() function we could avoid potentially traversing
  // the list of hosts twice.
  auto hosts = std::make_shared<HostVector>();

  for (const auto& host : original_host_set_.hosts()) {
    if (predicate(*host)) {
      matching_hosts.insert(host.get());
      hosts->emplace_back(host);
    }
  }

  auto healthy_hosts = std::make_shared<HealthyHostVector>();
  healthy_hosts->get().reserve(original_host_set_.healthyHosts().size());
  for (const auto& host : original_host_set_.healthyHosts()) {
    if (cached_predicate(*host)) {
      healthy_hosts->get().emplace_back(host);
    }
  }

  // ENVOY_LOG(info, "### HostSubsetImpl::update:3");
  auto degraded_hosts = std::make_shared<DegradedHostVector>();
  degraded_hosts->get().reserve(original_host_set_.degradedHosts().size());
  for (const auto& host : original_host_set_.degradedHosts()) {
    if (cached_predicate(*host)) {
      degraded_hosts->get().emplace_back(host);
    }
  }

  auto excluded_hosts = std::make_shared<ExcludedHostVector>();
  excluded_hosts->get().reserve(original_host_set_.excludedHosts().size());
  for (const auto& host : original_host_set_.excludedHosts()) {
    if (cached_predicate(*host)) {
      excluded_hosts->get().emplace_back(host);
    }
  }

  // If we only have one locality we can avoid the first call to filter() by
  // just creating a new HostsPerLocality from the list of all hosts.
  HostsPerLocalityConstSharedPtr hosts_per_locality;

  if (original_host_set_.hostsPerLocality().get().size() == 1) {
    hosts_per_locality = std::make_shared<HostsPerLocalityImpl>(
        *hosts, original_host_set_.hostsPerLocality().hasLocalLocality());
  } else {
    hosts_per_locality = original_host_set_.hostsPerLocality().filter({cached_predicate})[0];
  }

  auto healthy_hosts_per_locality =
      original_host_set_.healthyHostsPerLocality().filter({cached_predicate})[0];
  auto degraded_hosts_per_locality =
      original_host_set_.degradedHostsPerLocality().filter({cached_predicate})[0];
  auto excluded_hosts_per_locality =
      original_host_set_.excludedHostsPerLocality().filter({cached_predicate})[0];

  // We can use the cached predicate here, since we trust that the hosts in hosts_added were also
  // present in the list of all hosts.
  HostVector filtered_added;
  for (const auto& host : hosts_added) {
    if (cached_predicate(*host)) {
      filtered_added.emplace_back(host);
    }
  }

  // Since the removed hosts would not be present in the list of all hosts, we need to evaluate
  // the predicate directly for these hosts.
  HostVector filtered_removed;
  for (const auto& host : hosts_removed) {
    if (predicate(*host)) {
      filtered_removed.emplace_back(host);
    }
  }
  HostSetImpl::updateHosts(HostSetImpl::updateHostsParams(
                               hosts, hosts_per_locality, healthy_hosts, healthy_hosts_per_locality,
                               degraded_hosts, degraded_hosts_per_locality, excluded_hosts,
                               excluded_hosts_per_locality),
                           {}, filtered_added,
                           filtered_removed, absl::nullopt);
}

HostSetImplPtr ShuffleSubsetLoadBalancer::PriorityShuffleSubsetImpl::createHostSet(
    uint32_t priority, absl::optional<uint32_t> overprovisioning_factor) {
  // Use original hostset's overprovisioning_factor.
  ENVOY_LOG(info, "### PriorityShuffleSubsetImpl::createHostSet");
  RELEASE_ASSERT(priority < original_priority_set_.hostSetsPerPriority().size(), "");

  const HostSetPtr& host_set = original_priority_set_.hostSetsPerPriority()[priority];

  ASSERT(!overprovisioning_factor.has_value() ||
         overprovisioning_factor.value() == host_set->overprovisioningFactor());

  return HostSetImplPtr{
    new HostSubsetImpl(*host_set)
  };

}

void ShuffleSubsetLoadBalancer::PriorityShuffleSubsetImpl::update(uint32_t priority,
                                                    const HostVector& hosts_added,
                                                    const HostVector& hosts_removed) {
  ENVOY_LOG(info, "### PriorityShuffleSubsetImpl::update");
  getOrCreateHostSet(priority);
  updateSubset(priority, hosts_added, hosts_removed);

  // Create a new worker local LB if needed.
  // TODO(mattklein123): See the PrioritySubsetImpl constructor for additional comments on how
  // we can do better here.
  if (thread_aware_lb_ != nullptr) {
    lb_ = thread_aware_lb_->factory()->create();
  }
}

void ShuffleSubsetLoadBalancer::PriorityShuffleSubsetImpl::updateSubset(uint32_t priority, const HostVector& hosts_added, const HostVector& hosts_removed) {
  uint32_t *counter = new uint32_t(0);
  std::vector<uint32_t>::reverse_iterator begin = set_->rbegin();
  std::vector<uint32_t>::reverse_iterator * it = &begin;

  HostPredicate predicate = [counter, it](const Host&) -> bool {
    if (**it == (*counter)++) {
      (*it)++;
      return true;
    }
    return false;
  };

  reinterpret_cast<HostSubsetImpl*>(host_sets_[priority].get())
      ->update(hosts_added, hosts_removed, predicate);

  runUpdateCallbacks(hosts_added, hosts_removed);
}


} // namespace Upstream
} // namespace Envoy
