#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/macros.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

class SubsetLoadBalancer : public LoadBalancer, Logger::Loggable<Logger::Id::upstream> {
public:
  SubsetLoadBalancer(
      LoadBalancerType lb_type, HostSet& host_set, const HostSet* local_host_set,
      ClusterStats& stats, Runtime::Loader& runtime, Runtime::RandomGenerator& random,
      const LoadBalancerSubsetInfo& subsets,
      const Optional<envoy::api::v2::Cluster::RingHashLbConfig>& lb_ring_hash_config);

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  typedef std::function<bool(const Host&)> HostPredicate;

  // Represents a subset of an original HostSet.
  class HostSubsetImpl : public HostSetImpl {
  public:
    HostSubsetImpl(const HostSet& original_host_set)
        : HostSetImpl(), original_host_set_(original_host_set) {}

    void update(const std::vector<HostSharedPtr>& hosts_added,
                const std::vector<HostSharedPtr>& hosts_removed, HostPredicate predicate);

    void triggerCallbacks() { HostSetImpl::runUpdateCallbacks({}, {}); }
    bool empty() { return hosts().empty(); }

  private:
    const HostSet& original_host_set_;
  };

  typedef std::shared_ptr<HostSubsetImpl> HostSubsetImplPtr;

  typedef std::vector<std::pair<std::string, ProtobufWkt::Value>> SubsetMetadata;

  class LbSubsetEntry;
  typedef std::shared_ptr<LbSubsetEntry> LbSubsetEntryPtr;
  typedef std::unordered_map<HashedValue, LbSubsetEntryPtr> ValueSubsetMap;
  typedef std::unordered_map<std::string, ValueSubsetMap> LbSubsetMap;

  // Entry in the subset hierarchy.
  class LbSubsetEntry {
  public:
    LbSubsetEntry() {}

    bool initialized() const { return lb_ != nullptr && host_subset_ != nullptr; }
    bool active() const { return initialized() && !host_subset_->empty(); }

    void initLoadBalancer(const SubsetLoadBalancer& subset_lb, HostPredicate predicate);

    LbSubsetMap children_;

    // Only initialized if a match exists at this level.
    HostSubsetImplPtr host_subset_;
    LoadBalancerPtr lb_;
  };

  // Implements HostSet::MemberUpdateCb
  void update(const std::vector<HostSharedPtr>& hosts_added,
              const std::vector<HostSharedPtr>& hosts_removed);

  void updateFallbackSubset(const std::vector<HostSharedPtr>& hosts_added,
                            const std::vector<HostSharedPtr>& hosts_removed);
  void processSubsets(const std::vector<HostSharedPtr>& hosts_added,
                      const std::vector<HostSharedPtr>& hosts_removed,
                      std::function<void(LbSubsetEntryPtr, HostPredicate, bool)> cb);

  HostConstSharedPtr tryChooseHostFromContext(LoadBalancerContext* context, bool& host_chosen);

  bool hostMatchesDefaultSubset(const Host& host);
  bool hostMatches(const SubsetMetadata& kvs, const Host& host);

  LbSubsetEntryPtr
  findSubset(const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches);

  LbSubsetEntryPtr findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs,
                                      uint32_t idx);

  SubsetMetadata extractSubsetMetadata(const std::set<std::string>& subset_keys, const Host& host);

  const HostSetImpl& emptyHostSet() { CONSTRUCT_ON_FIRST_USE(HostSetImpl); };

  const LoadBalancerType lb_type_;
  const Optional<envoy::api::v2::Cluster::RingHashLbConfig> lb_ring_hash_config_;
  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

  const envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallback_policy_;
  const ProtobufWkt::Struct default_subset_;
  const std::vector<std::set<std::string>> subset_keys_;

  const HostSet& original_host_set_;
  const HostSet* original_local_host_set_;

  LbSubsetEntryPtr fallback_subset_;

  // Forms a trie-like structure. Requires lexically sorted Host and Route metadata.
  LbSubsetMap subsets_;
};

} // namespace Upstream
} // namespace Envoy
