#pragma once

#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/router/delegating_route_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/router/per_filter_config.h"

namespace Envoy {
namespace Router {

using WeightedClusterProto = envoy::config::route::v3::WeightedCluster;
using ClusterWeightProto = envoy::config::route::v3::WeightedCluster::ClusterWeight;

class WeightedClusterEntry;
class WeightedClusterSpecifierPlugin;

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

  Runtime::Loader& loader_;
  const Http::LowerCaseString random_value_header_;
  const std::string runtime_key_prefix_;
  const bool use_hash_policy_{};
  std::vector<WeightedClustersConfigEntryConstSharedPtr> weighted_clusters_;
  uint64_t total_cluster_weight_{0};
};

} // namespace Router
} // namespace Envoy
