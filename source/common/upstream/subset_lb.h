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
    Optional<uint32_t> findLocalityIndex(const Host& host);
    void addKnownHosts(HostVectorSharedPtr known_hosts, HostVectorConstSharedPtr hosts);

    const HostSet& original_host_set_;
    std::unordered_map<HostSharedPtr, Optional<uint32_t>> host_to_locality_;
    bool empty_;
  };

  // Subset maintains a LoadBalancer and HostSubsetImpl for a subset's filtered hosts.
  class Subset {
  public:
    Subset(const HostSet& original_host_set) : host_set_(HostSubsetImpl(original_host_set)) {}

    bool empty() { return host_set_.empty(); }

    HostSubsetImpl host_set_;
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
              const std::vector<HostSharedPtr>& hosts_removed);

  HostConstSharedPtr tryChooseHostFromContext(LoadBalancerContext* context, bool& host_chosen);

  bool hostMatchesDefaultSubset(const Host& host);
  bool hostMatches(const SubsetMetadata& kvs, const Host& host);

  SubsetPtr findSubset(const std::vector<Router::MetadataMatchCriterionConstSharedPtr>& matches);

  LbSubsetEntryPtr findOrCreateSubset(LbSubsetMap& subsets, const SubsetMetadata& kvs, uint32_t idx,
                                      bool allow_create);

  SubsetPtr newSubset(std::function<bool(const Host&)> predicate);

  bool extractSubsetMetadata(const std::set<std::string>& subset_keys, const Host& host,
                             SubsetMetadata& kvs);

  LoadBalancer* newLoadBalancer(const SubsetPtr& subset);

  const HostSetImpl& emptyHostSet() { CONSTRUCT_ON_FIRST_USE(HostSetImpl); };
};

} // namespace Upstream
} // namespace Envoy
