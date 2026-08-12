#include "source/extensions/router/cluster_specifiers/priority_group/priority_group_cluster_specifier.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {
namespace {

constexpr absl::string_view NameField = "name";
constexpr absl::string_view ClustersField = "clusters";
constexpr absl::string_view WeightField = "weight";

// Get the string value of the given field. Returns nullopt if the field is missing or is not a
// non-empty string.
std::optional<std::string> stringField(const Protobuf::Map<std::string, Protobuf::Value>& fields,
                                       absl::string_view name) {
  const auto iter = fields.find(name);
  if (iter == fields.end() || iter->second.kind_case() != Protobuf::Value::kStringValue ||
      iter->second.string_value().empty()) {
    return std::nullopt;
  }
  return iter->second.string_value();
}

} // namespace

PriorityGroupEntry::PriorityGroupEntry(const PriorityGroupProto& proto) : name_(proto.name()) {
  clusters_.reserve(proto.clusters().size());
  for (const ClusterWeightProto& cluster : proto.clusters()) {
    clusters_.emplace_back(cluster.name(), cluster.weight().value());
    total_weight_ += cluster.weight().value();
  }
  // Every group has at least one cluster and every cluster weight is at least 1, so the total
  // weight of a group is always greater than 0. Both are enforced by the proto validation rules.
  ASSERT(total_weight_ > 0);
}

PriorityGroupEntry::PriorityGroupEntry(std::string name,
                                       std::vector<std::pair<std::string, uint64_t>> clusters)
    : name_(std::move(name)), clusters_(std::move(clusters)) {
  for (const auto& cluster : clusters_) {
    total_weight_ += cluster.second;
  }
}

std::optional<PriorityGroupEntry>
PriorityGroupEntry::parseFromMetadata(const Protobuf::Value& value) {
  if (value.kind_case() != Protobuf::Value::kStructValue) {
    return std::nullopt;
  }
  const auto& fields = value.struct_value().fields();

  // The group name is always required. It is used to select the configured group that provides
  // the clusters if the clusters are not overridden by the metadata.
  std::optional<std::string> name = stringField(fields, NameField);
  if (!name.has_value()) {
    return std::nullopt;
  }

  const auto clusters_iter = fields.find(ClustersField);
  if (clusters_iter == fields.end()) {
    // Only the group name is overridden by the metadata.
    return PriorityGroupEntry(std::move(*name), {});
  }
  if (clusters_iter->second.kind_case() != Protobuf::Value::kListValue) {
    return std::nullopt;
  }

  const auto& cluster_values = clusters_iter->second.list_value().values();
  std::vector<std::pair<std::string, uint64_t>> clusters;
  clusters.reserve(cluster_values.size());
  for (const auto& cluster_value : cluster_values) {
    if (cluster_value.kind_case() != Protobuf::Value::kStructValue) {
      return std::nullopt;
    }
    const auto& cluster_fields = cluster_value.struct_value().fields();

    std::optional<std::string> cluster_name = stringField(cluster_fields, NameField);
    if (!cluster_name.has_value()) {
      return std::nullopt;
    }

    const auto weight_iter = cluster_fields.find(WeightField);
    if (weight_iter == cluster_fields.end() ||
        weight_iter->second.kind_case() != Protobuf::Value::kNumberValue) {
      return std::nullopt;
    }
    // The comparison also rejects NaN. The weight of a cluster must be in the same range as the
    // weight of a configured cluster.
    const double weight = weight_iter->second.number_value();
    if (!(weight >= 1.0) || weight > std::numeric_limits<uint32_t>::max()) {
      return std::nullopt;
    }

    clusters.emplace_back(std::move(*cluster_name), static_cast<uint64_t>(weight));
  }

  // An empty cluster list is treated the same way as a missing cluster list: only the group name
  // is overridden by the metadata.
  return PriorityGroupEntry(std::move(*name), std::move(clusters));
}

const std::string& PriorityGroupEntry::selectCluster(uint64_t random_value) const {
  // The group has no cluster at all or all the cluster weights are 0. Both the proto validation
  // rules and the metadata parsing reject these cases, so this should never happen.
  if (total_weight_ == 0) {
    IS_ENVOY_BUG("cluster selection on a priority group without any weighted cluster");
    return EMPTY_STRING;
  }

  const uint64_t selected_value = random_value % total_weight_;

  // Find the target cluster based on the interval in which the selected value falls. The
  // intervals are determined as [0, cluster1_weight), [cluster1_weight,
  // cluster1_weight + cluster2_weight), ...
  uint64_t begin = 0;
  for (const auto& cluster : clusters_) {
    const uint64_t end = begin + cluster.second;
    if (selected_value >= begin && selected_value < end) {
      return cluster.first;
    }
    begin = end;
  }

  // The total weight is greater than 0 and the selected value is less than the total weight, so
  // one cluster must have been selected above.
  IS_ENVOY_BUG("unexpected weighted cluster selection result");
  return clusters_.back().first;
}

PriorityGroupClusterSpecifierPlugin::PriorityGroupClusterSpecifierPlugin(
    const PriorityGroupClusterSpecifierConfigProto& proto) {
  groups_.reserve(proto.priority_groups().size());
  groups_by_name_.reserve(proto.priority_groups().size());
  for (const PriorityGroupProto& group_proto : proto.priority_groups()) {
    groups_.push_back(std::make_unique<PriorityGroupEntry>(group_proto));
    // If multiple groups share the same name, then only the first one of them could be
    // referenced by the group override metadata.
    groups_by_name_.emplace(groups_.back()->name(), groups_.back().get());
  }
  if (proto.has_group_override_metadata()) {
    group_override_metadata_.emplace(proto.group_override_metadata());
  }
}

std::optional<PriorityGroupEntry> PriorityGroupClusterSpecifierPlugin::groupOverrideForAttempt(
    const StreamInfo::StreamInfo& stream_info, uint64_t attempt_index) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(&stream_info.dynamicMetadata(),
                                                             *group_override_metadata_);
  if (value.kind_case() != Protobuf::Value::kListValue) {
    return std::nullopt;
  }

  const auto& overrides = value.list_value().values();
  if (overrides.empty()) {
    return std::nullopt;
  }

  // Wrap around if there are more attempts than the group overrides in the metadata.
  return PriorityGroupEntry::parseFromMetadata(overrides.at(attempt_index % overrides.size()));
}

const PriorityGroupEntry*
PriorityGroupClusterSpecifierPlugin::configuredGroup(const std::string& name) const {
  const auto group_iter = groups_by_name_.find(name);
  return group_iter == groups_by_name_.end() ? nullptr : group_iter->second;
}

std::string
PriorityGroupClusterSpecifierPlugin::selectCluster(const StreamInfo::StreamInfo& stream_info,
                                                   uint64_t random_value) const {
  // The attempt count is 1 for the initial attempt, 2 for the first retry and so on. It may not
  // be set yet when the route is selected before the router filter starts the initial attempt, so
  // fall back to the initial attempt in that case.
  const uint32_t attempt_count = std::max<uint32_t>(stream_info.attemptCount().value_or(1), 1);
  const uint64_t attempt_index = attempt_count - 1;

  // The groups may be overridden by the dynamic metadata on a per-request basis.
  if (group_override_metadata_.has_value()) {
    const auto group_override = groupOverrideForAttempt(stream_info, attempt_index);
    if (group_override.has_value()) {
      // The metadata also overrides the clusters of the group, so the clusters and the weights of
      // the metadata are used directly. Note these clusters are not validated at the config load
      // time and the request fails if the selected cluster doesn't exist.
      if (!group_override->clusters().empty()) {
        return group_override->selectCluster(random_value);
      }

      // Only the group name is overridden, so the clusters and the weights of the configured group
      // with the same name are used.
      if (const auto* group = configuredGroup(group_override->name()); group != nullptr) {
        return group->selectCluster(random_value);
      }
      ENVOY_LOG(debug,
                "priority group cluster specifier: unknown group '{}' in the group override "
                "metadata; falling back to the configured group order",
                group_override->name());
    }
  }

  // Wrap around if there are more attempts than the configured groups.
  return groups_[attempt_index % groups_.size()]->selectCluster(random_value);
}

/**
 * Route entry that selects the target cluster of the priority groups. The cluster is re-selected
 * on every refreshRouteCluster() call because the attempt count of the request, and then the
 * selected priority group, may have been changed.
 */
class PriorityGroupRouteEntry : public Envoy::Router::DynamicRouteEntry {
public:
  PriorityGroupRouteEntry(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                          std::shared_ptr<const PriorityGroupClusterSpecifierPlugin> plugin,
                          std::string&& cluster_name, uint64_t random_value)
      : DynamicRouteEntry(std::move(parent), std::move(cluster_name)), plugin_(std::move(plugin)),
        random_value_(random_value) {
    ASSERT(plugin_ != nullptr);
  }

  void refreshRouteCluster(const Http::RequestHeaderMap&,
                           const StreamInfo::StreamInfo& stream_info) const override {
    // Dynamic route entry is created for each request and only used for single request in single
    // thread. So, it is safe to update the cluster name in the route entry without worrying about
    // thread safety or affecting other requests.
    const_cast<PriorityGroupRouteEntry*>(this)->cluster_name_ =
        plugin_->selectCluster(stream_info, random_value_);
  }

private:
  const std::shared_ptr<const PriorityGroupClusterSpecifierPlugin> plugin_;
  const uint64_t random_value_{};
};

Envoy::Router::RouteConstSharedPtr PriorityGroupClusterSpecifierPlugin::route(
    Envoy::Router::RouteEntryAndRouteConstSharedPtr parent, const Http::RequestHeaderMap&,
    const StreamInfo::StreamInfo& stream_info, uint64_t random) const {
  std::string cluster_name = selectCluster(stream_info, random);
  return std::make_shared<PriorityGroupRouteEntry>(std::move(parent), shared_from_this(),
                                                   std::move(cluster_name), random);
}

absl::Status
PriorityGroupClusterSpecifierPlugin::validateClusters(const Upstream::ClusterManager& cm) const {
  for (const auto& group : groups_) {
    for (const auto& cluster : group->clusters()) {
      if (!cm.hasCluster(cluster.first)) {
        return absl::InvalidArgumentError(fmt::format(
            "route: unknown cluster '{}' in priority group '{}'", cluster.first, group->name()));
      }
    }
  }
  return absl::OkStatus();
}

} // namespace PriorityGroup
} // namespace Router
} // namespace Extensions
} // namespace Envoy
