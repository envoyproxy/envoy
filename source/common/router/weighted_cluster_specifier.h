#pragma once

#include "envoy/router/cluster_specifier_plugin.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/router/delegating_route_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/per_filter_config.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Router {

using WeightedClusterProto = envoy::config::route::v3::WeightedCluster;
using ClusterWeightProto = envoy::config::route::v3::WeightedCluster::ClusterWeight;

class WeightedClusterEntry;
class WeightedClusterSpecifierPlugin;

/**
 * Filter state key for tracking attempted weighted clusters during retries.
 * This allows the weighted cluster selection logic to avoid re-selecting
 * clusters that have already been tried and failed.
 */
inline constexpr absl::string_view kWeightedClusterAttemptedClustersKey =
    "envoy.weighted_cluster.attempted_clusters";

/**
 * FilterState object that tracks which weighted clusters have been attempted
 * during the lifetime of a request (including retries). When a cluster is
 * attempted and fails, its name is added to this set. On subsequent retries,
 * the weighted cluster selection logic will zero out the weight of any
 * previously-attempted clusters to ensure the retry lands on a different cluster.
 */
class AttemptedClustersFilterState : public StreamInfo::FilterState::Object {
public:
  /**
   * Record a cluster name as having been attempted.
   */
  void addAttemptedCluster(const std::string& cluster_name) {
    attempted_clusters_.insert(cluster_name);
  }

  /**
   * Check if a cluster has been previously attempted.
   */
  bool hasAttempted(const std::string& cluster_name) const {
    return attempted_clusters_.contains(cluster_name);
  }

  /**
   * @return the number of clusters that have been attempted.
   */
  size_t size() const { return attempted_clusters_.size(); }

  absl::optional<std::string> serializeAsString() const override {
    return absl::StrJoin(attempted_clusters_, ",");
  }

private:
  absl::flat_hash_set<std::string> attempted_clusters_;
};

struct WeightedClustersConfigEntry {
public:
  static absl::StatusOr<std::shared_ptr<WeightedClustersConfigEntry>>
  create(const ClusterWeightProto& cluster, const MetadataMatchCriteria* parent_metadata_match,
         std::string&& runtime_key, Server::Configuration::ServerFactoryContext& context);

  uint64_t clusterWeight(Runtime::Loader& loader) const {
    return loader.snapshot().getInteger(runtime_key_, cluster_weight_);
  }

private:
  friend class WeightedClusterEntry;
  friend class WeightedClusterSpecifierPlugin;

  WeightedClustersConfigEntry(const ClusterWeightProto& cluster,
                              const MetadataMatchCriteria* parent_metadata_match,
                              std::string&& runtime_key,
                              Server::Configuration::ServerFactoryContext& context);

  const std::string runtime_key_;
  const uint64_t cluster_weight_;
  MetadataMatchCriteriaConstPtr cluster_metadata_match_criteria_;
  HeaderParserPtr request_headers_parser_;
  HeaderParserPtr response_headers_parser_;
  std::unique_ptr<PerFilterConfigs> per_filter_configs_;
  const std::string host_rewrite_;
  const std::string cluster_name_;
  const Http::LowerCaseString cluster_header_name_;
};

using WeightedClustersConfigEntryConstSharedPtr =
    std::shared_ptr<const WeightedClustersConfigEntry>;

class WeightedClusterSpecifierPlugin : public ClusterSpecifierPlugin,
                                       public Logger::Loggable<Logger::Id::router> {
public:
  WeightedClusterSpecifierPlugin(const WeightedClusterProto& weighted_clusters,
                                 const MetadataMatchCriteria* parent_metadata_match,
                                 absl::string_view route_name,
                                 Server::Configuration::ServerFactoryContext& context,
                                 absl::Status& creation_status);

  RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                            const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo&,
                            uint64_t random) const override;

  absl::Status validateClusters(const Upstream::ClusterManager& cm) const override;

private:
  RouteConstSharedPtr pickWeightedCluster(RouteEntryAndRouteConstSharedPtr parent,
                                          const Http::RequestHeaderMap& headers,
                                          const StreamInfo::StreamInfo& stream_info,
                                          uint64_t random_value) const;
  uint64_t healthawareClusterWeight(const std::string& cluster_name, uint64_t config_weight) const;
  uint64_t retryAwareClusterWeight(const std::string& cluster_name, uint64_t config_weight,
                                   const AttemptedClustersFilterState* attempted_clusters) const;

  Runtime::Loader& loader_;
  Upstream::ClusterManager& cluster_manager_;
  const Http::LowerCaseString random_value_header_;
  const std::string runtime_key_prefix_;
  const bool use_hash_policy_{};
  const bool health_aware_lb_{true};
  const bool retry_aware_lb_{true};
  std::vector<WeightedClustersConfigEntryConstSharedPtr> weighted_clusters_;
  uint64_t total_cluster_weight_{0};
};

} // namespace Router
} // namespace Envoy
