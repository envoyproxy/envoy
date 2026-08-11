#include "source/extensions/router/cluster_specifiers/priority_group/priority_group_cluster_specifier.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/router/delegating_route_impl.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace PriorityGroup {

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

const std::string& PriorityGroupEntry::selectCluster(uint64_t random_value) const {
  ASSERT(total_weight_ > 0);
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

const PriorityGroupEntry* PriorityGroupClusterSpecifierPlugin::groupFromOverrideMetadata(
    const StreamInfo::StreamInfo& stream_info, uint64_t attempt_index) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(&stream_info.dynamicMetadata(),
                                                             *group_override_metadata_);
  if (value.kind_case() != Protobuf::Value::kListValue) {
    return nullptr;
  }

  const auto& overrides = value.list_value().values();
  if (overrides.empty()) {
    return nullptr;
  }

  // Wrap around if there are more attempts than the group overrides in the metadata.
  const auto& group_override = overrides.at(attempt_index % overrides.size());
  if (group_override.kind_case() != Protobuf::Value::kStructValue) {
    return nullptr;
  }

  // Only the group name is supported for now. More fields may be supported in the future, for
  // example to also override the weighted clusters of the group.
  const auto& fields = group_override.struct_value().fields();
  const auto name_iter = fields.find("name");
  if (name_iter == fields.end() || name_iter->second.kind_case() != Protobuf::Value::kStringValue) {
    return nullptr;
  }

  const auto group_iter = groups_by_name_.find(name_iter->second.string_value());
  if (group_iter == groups_by_name_.end()) {
    ENVOY_LOG(debug,
              "priority group cluster specifier: unknown group '{}' in the group override "
              "metadata; falling back to the configured group order",
              name_iter->second.string_value());
    return nullptr;
  }
  return group_iter->second;
}

const PriorityGroupEntry*
PriorityGroupClusterSpecifierPlugin::groupForAttempt(const StreamInfo::StreamInfo& stream_info,
                                                     uint64_t attempt_index) const {
  // The groups may be overridden by the dynamic metadata on a per-request basis.
  if (group_override_metadata_.has_value()) {
    if (const auto* group = groupFromOverrideMetadata(stream_info, attempt_index);
        group != nullptr) {
      return group;
    }
  }

  // Wrap around if there are more attempts than the configured groups.
  return groups_[attempt_index % groups_.size()].get();
}

const std::string&
PriorityGroupClusterSpecifierPlugin::selectCluster(const StreamInfo::StreamInfo& stream_info,
                                                   uint64_t random_value) const {
  // The attempt count is 1 for the initial attempt, 2 for the first retry and so on. It may not
  // be set yet when the route is selected before the router filter starts the initial attempt, so
  // fall back to the initial attempt in that case.
  const uint32_t attempt_count = std::max<uint32_t>(stream_info.attemptCount().value_or(1), 1);
  return groupForAttempt(stream_info, attempt_count - 1)->selectCluster(random_value);
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
