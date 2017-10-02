#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/upstream/load_balancer.h"

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

  // Upstream::LoadBalancer
  HostConstSharedPtr chooseHost(const LoadBalancerContext* context) override;

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

  // Represents a subset of an original HostSet.
  class HostSubsetImpl : public HostSetImpl {
  public:
    HostSubsetImpl(const HostSet& original_host_set)
        : HostSetImpl(), original_host_set_(original_host_set), empty_(true) {}

    void update(const std::vector<HostSharedPtr>& hosts_added,
                const std::vector<HostSharedPtr>& hosts_removed);

    void triggerCallbacks() { HostSetImpl::runUpdateCallbacks({}, {}); }

    bool empty() { return empty_; };

  private:
    int findLocalityIndex(const HostSharedPtr& host);
    void addKnownHosts(HostVectorSharedPtr known_hosts, HostVectorConstSharedPtr hosts);

    const HostSet& original_host_set_;
    std::unordered_map<HostSharedPtr, int> host_to_locality_;
    bool empty_;
  };

  // Subset maintains a LoadBalancer and HostSubsetImpls for a
  // subset's filtered hosts and filtered local hosts.
  class Subset {
  public:
    Subset(const HostSet& original_host_set, const HostSet& original_local_host_set)
        : host_set_(HostSubsetImpl(original_host_set)),
          local_host_set_(HostSubsetImpl(original_local_host_set)) {}

    bool empty() { return host_set_.empty() && local_host_set_.empty(); }

    HostSubsetImpl host_set_;
    HostSubsetImpl local_host_set_;
    LoadBalancerPtr lb_;
  };
  typedef std::shared_ptr<Subset> SubsetPtr;

  SubsetPtr default_host_subset_;

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
    SubsetPtr host_subset_;
  };

  // Forms a trie-like structure. Requires lexically sorted Host and Route metadata.
  LbSubsetMap subsets_;

  void update(const std::vector<HostSharedPtr>& hosts_added,
              const std::vector<HostSharedPtr>& hosts_removed, bool local);

  bool hostMatchesDefaultSubset(const HostSharedPtr& host);
  bool hostMatches(const SubsetMetadata& kvs, const HostSharedPtr& host);

  SubsetPtr findSubset(const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches);

  LbSubsetEntryPtr findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs, size_t idx,
                                      bool allow_create);

  SubsetPtr newSubset(std::function<bool(const HostSharedPtr&)> predicate);

  bool extractSubsetMetadata(const std::set<std::string>& subset_keys, const HostSharedPtr& host,
                             SubsetMetadata* kvs);

  LoadBalancer* newLoadBalancer(const SubsetPtr& subset);

  static const HostSetImpl empty_host_set_;
};

} // namespace Upstream
} // namespace Envoy
