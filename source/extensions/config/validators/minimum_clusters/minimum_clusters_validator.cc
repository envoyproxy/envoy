#include "source/extensions/config/validators/minimum_clusters/minimum_clusters_validator.h"

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Config {
namespace Validators {

void MinimumClustersValidator::validate(
    const Server::Instance&, const std::vector<Envoy::Config::DecodedResourcePtr>& resources) {
  absl::flat_hash_set<std::string> next_cluster_names(resources.size());
  for (const auto& resource : resources) {
    const envoy::config::cluster::v3::Cluster& cluster =
        dynamic_cast<const envoy::config::cluster::v3::Cluster&>(resource->resource());

    // If the cluster was already added in the current update, it won't be added twice.
    next_cluster_names.insert(cluster.name());
  }

  // After applying the update, the clusters names will be the same as in
  // next_cluster_names.
  if (next_cluster_names.size() < min_clusters_num_) {
    throw EnvoyException("CDS update attempts to reduce clusters below configured minimum.");
  }
}

void MinimumClustersValidator::validate(
    const Server::Instance& server,
    const std::vector<Envoy::Config::DecodedResourcePtr>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  const Upstream::ClusterManager& cm = server.clusterManager();
  // If the number of clusters after removing all of the clusters in the removed_resources list is
  // above the threshold, then it is surely a valid config.
  const Upstream::ClusterManager::ClusterInfoMaps& cur_clusters = cm.clusters();
  const uint32_t cur_clusters_num = cur_clusters.added_via_api_clusters_num_;
  const uint32_t removed_resources_size = static_cast<uint32_t>(removed_resources.size());
  if ((cur_clusters_num >= removed_resources_size) &&
      (cur_clusters_num - removed_resources_size >= min_clusters_num_)) {
    return;
  }

  // It could be that the removed clusters gets us below the threshold, simulate what happens if
  // the current clusters list is updated.
  uint32_t newly_added_clusters_num = 0;
  absl::flat_hash_set<std::string> added_cluster_names(added_resources.size());
  for (const auto& resource : added_resources) {
    const envoy::config::cluster::v3::Cluster& cluster =
        dynamic_cast<const envoy::config::cluster::v3::Cluster&>(resource->resource());

    // If the cluster was already added in the current update, skip this cluster.
    if (!added_cluster_names.insert(cluster.name()).second) {
      continue;
    }
    // If the cluster is new, count it.
    if (!cur_clusters.hasCluster(cluster.name())) {
      ++newly_added_clusters_num;
    }
  }

  // Count the clusters that need to be removed.
  uint32_t removed_clusters_num = 0;
  for (const auto& removed_cluster : removed_resources) {
    Upstream::ClusterConstOptRef cluster = cur_clusters.getCluster(removed_cluster);
    // Only clusters that were added via api can be removed.
    if (cluster.has_value() && cluster->get().info()->addedViaApi()) {
      ++removed_clusters_num;
    }
  }

  // Prevent integer overflow.
  ASSERT(cur_clusters_num >= removed_clusters_num);
  const uint64_t new_clusters_num =
      static_cast<uint64_t>(cur_clusters_num) + newly_added_clusters_num - removed_clusters_num;
  if (new_clusters_num < min_clusters_num_) {
    throw EnvoyException("CDS update attempts to reduce clusters below configured minimum.");
  }
}

} // namespace Validators
} // namespace Config
} // namespace Extensions
} // namespace Envoy
