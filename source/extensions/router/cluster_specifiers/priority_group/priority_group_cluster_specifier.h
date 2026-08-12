#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/router/cluster_specifiers/priority_group/v3/priority_group.pb.h"
#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/common/logger.h"
#include "source/common/config/metadata.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {

using PriorityGroupClusterSpecifierConfigProto = envoy::extensions::router::cluster_specifiers::
    priority_group::v3::PriorityGroupClusterSpecifier;
using PriorityGroupProto =
    envoy::extensions::router::cluster_specifiers::priority_group::v3::PriorityGroup;
using ClusterWeightProto =
    envoy::extensions::router::cluster_specifiers::priority_group::v3::ClusterWeight;

/**
 * A single named group of weighted clusters. All the clusters of the group have the same
 * priority and the target cluster is selected from the group based on the weights.
 */
class PriorityGroupEntry {
public:
  explicit PriorityGroupEntry(const PriorityGroupProto& proto);
  PriorityGroupEntry(std::string name, std::vector<std::pair<std::string, uint64_t>> clusters);

  /**
   * Parse one element of the group override metadata list into a group entry. The returned entry
   * may have no cluster at all if the metadata only overrides the group name.
   * @param value one element of the group override metadata list.
   * @return the parsed group entry or nullopt if the given value is not a valid group override.
   */
  static std::optional<PriorityGroupEntry> parseFromMetadata(const Protobuf::Value& value);

  const std::string& name() const { return name_; }

  /**
   * Select one cluster of this group based on the given random value. This should only be called
   * if the total weight of the group is greater than 0.
   * @param random_value random value used for the weighted selection.
   * @return const std::string& name of the selected cluster.
   */
  const std::string& selectCluster(uint64_t random_value) const;

  /**
   * @return all the candidate cluster names of this group.
   */
  const std::vector<std::pair<std::string, uint64_t>>& clusters() const { return clusters_; }

private:
  std::string name_;
  // Pairs of the cluster name and the cluster weight.
  std::vector<std::pair<std::string, uint64_t>> clusters_;
  uint64_t total_weight_{};
};

using PriorityGroupEntryPtr = std::unique_ptr<PriorityGroupEntry>;

/**
 * Cluster specifier plugin that selects a priority group based on the attempt count of the
 * request first and then selects a cluster of the group based on the cluster weights.
 */
class PriorityGroupClusterSpecifierPlugin
    : public Envoy::Router::ClusterSpecifierPlugin,
      public Logger::Loggable<Logger::Id::router>,
      public std::enable_shared_from_this<PriorityGroupClusterSpecifierPlugin> {
public:
  explicit PriorityGroupClusterSpecifierPlugin(
      const PriorityGroupClusterSpecifierConfigProto& proto);

  Envoy::Router::RouteConstSharedPtr route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                                           const Http::RequestHeaderMap& headers,
                                           const StreamInfo::StreamInfo& stream_info,
                                           uint64_t random) const override;

  absl::Status validateClusters(const Upstream::ClusterManager& cm) const override;

  /**
   * Select the target cluster for the current attempt of the request.
   * @param stream_info stream info of the downstream request. Used to get the attempt count and
   *        the optional group override metadata.
   * @param random_value random value used for the weighted cluster selection.
   * @return std::string name of the selected cluster.
   */
  std::string selectCluster(const StreamInfo::StreamInfo& stream_info, uint64_t random_value) const;

private:
  // Get the priority group override of the given attempt from the group override metadata. Returns
  // nullopt if the metadata is not available or the override of the attempt is not valid. The
  // returned entry may have no cluster at all if the metadata only overrides the group name.
  std::optional<PriorityGroupEntry>
  groupOverrideForAttempt(const StreamInfo::StreamInfo& stream_info, uint64_t attempt_index) const;

  // Get the configured priority group by name. Returns nullptr if there is no such group.
  const PriorityGroupEntry* configuredGroup(const std::string& name) const;

  std::vector<PriorityGroupEntryPtr> groups_;
  // Index of the groups by the group name. The values point to the entries owned by groups_.
  absl::flat_hash_map<std::string, const PriorityGroupEntry*> groups_by_name_;
  // Optional dynamic metadata key that provides the per-request group override.
  std::optional<Envoy::Config::MetadataKey> group_override_metadata_;
};

using PriorityGroupClusterSpecifierPluginSharedPtr =
    std::shared_ptr<PriorityGroupClusterSpecifierPlugin>;

} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
