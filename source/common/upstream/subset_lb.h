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
  SubsetLoadBalancer(LoadBalancerType lb_type, HostSet& host_set, const HostSet* local_host_set,
                     ClusterStats& stats, Runtime::Loader& runtime,
                     Runtime::RandomGenerator& random, const LoadBalancerSubsetInfo& subsets);
  ~SubsetLoadBalancer();

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(LoadBalancerContext* context) override;

private:
  LoadBalancerType lb_type_;
  ClusterStats& stats_;
  Runtime::Loader& runtime_;
  Runtime::RandomGenerator& random_;

  envoy::api::v2::Cluster::LbSubsetConfig::LbSubsetFallbackPolicy fallback_policy_;
  ProtobufWkt::Struct default_subset_;
  std::vector<std::set<std::string>> subset_keys_;

  const HostSet& original_host_set_;
  const HostSet* original_local_host_set_;

  typedef std::function<bool(const Host&)> HostPredicate;

  // Represents a subset of an original HostSet.
  class HostSubsetImpl : public HostSetImpl {
  public:
    HostSubsetImpl(const HostSet& original_host_set)
        : HostSetImpl(), original_host_set_(original_host_set), empty_(true) {}

    void update(const std::vector<HostSharedPtr>& hosts_added,
                const std::vector<HostSharedPtr>& hosts_removed, HostPredicate predicate);

    void triggerCallbacks() { HostSetImpl::runUpdateCallbacks({}, {}); }

    bool empty() { return empty_; };

  private:
    const HostSet& original_host_set_;
    bool empty_;
  };

  typedef std::shared_ptr<HostSubsetImpl> HostSubsetImplPtr;

  class LbSubsetEntry;
  typedef std::shared_ptr<LbSubsetEntry> LbSubsetEntryPtr;
  typedef std::unordered_map<HashedValue, LbSubsetEntryPtr> ValueSubsetMap;
  typedef std::unordered_map<std::string, ValueSubsetMap> LbSubsetMap;
  typedef std::vector<std::pair<std::string, ProtobufWkt::Value>> SubsetMetadata;

  // Entry in the subset hierarchy.
  class LbSubsetEntry {
  public:
    LbSubsetEntry() {}

    LbSubsetMap children_;

    // Match exists at this level
    HostSubsetImplPtr host_subset_;
    LoadBalancerPtr lb_;

    bool initialized() const { return lb_ != nullptr && host_subset_ != nullptr; }
    bool active() const { return initialized() && !host_subset_->empty(); }

    void initLoadBalancer(const SubsetLoadBalancer& subset_lb, HostPredicate predicate);
  };

  LbSubsetEntryPtr fallback_subset_;

  // Forms a trie-like structure. Requires lexically sorted Host and Route metadata.
  LbSubsetMap subsets_;

  // Implements MemberUpdateCb
  void update(const std::vector<HostSharedPtr>& hosts_added,
              const std::vector<HostSharedPtr>& hosts_removed);

  HostConstSharedPtr tryChooseHostFromContext(LoadBalancerContext* context, bool& host_chosen);

  bool hostMatchesDefaultSubset(const Host& host);
  bool hostMatches(const SubsetMetadata& kvs, const Host& host);

  LbSubsetEntryPtr
  findSubset(const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches);

  LbSubsetEntryPtr findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs,
                                      uint32_t idx);

  SubsetMetadata extractSubsetMetadata(const std::set<std::string>& subset_keys, const Host& host);

  const HostSetImpl& emptyHostSet() { CONSTRUCT_ON_FIRST_USE(HostSetImpl); };
};

} // namespace Upstream
} // namespace Envoy
