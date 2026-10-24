#include "source/extensions/router/cluster_specifiers/header_weighted/header_weighted.h"

#include "envoy/upstream/cluster_manager.h"

#include "source/common/router/delegating_route_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {

namespace {

uint64_t computeTotalWeight(const HeaderWeightedConfigProto& config) {
  uint64_t total = 0;
  for (const auto& cluster : config.clusters()) {
    total += cluster.weight();
  }
  return total;
}

} // namespace

HeaderWeightedClusterSpecifierPlugin::HeaderWeightedClusterSpecifierPlugin(
    const HeaderWeightedConfigProto& config)
    : header_(config.header()),
      clusters_(config.clusters().begin(), config.clusters().end()),
      total_weight_(computeTotalWeight(config)),
      default_cluster_(config.default_cluster()) {}

absl::Status HeaderWeightedClusterSpecifierPlugin::validateClusters(
    const Upstream::ClusterManager& cm) const {
  for (const auto& cluster : clusters_) {
    if (!cm.hasCluster(cluster.name())) {
      return absl::InvalidArgumentError(absl::StrCat(
          "header weighted cluster specifier: unknown cluster '",
          cluster.name(), "'"));
    }
  }
  if (!cm.hasCluster(default_cluster_)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "header weighted cluster specifier: unknown default cluster '",
        default_cluster_, "'"));
  }
  return absl::OkStatus();
}

Envoy::Router::RouteConstSharedPtr HeaderWeightedClusterSpecifierPlugin::route(
    Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
    const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo&, uint64_t random) const {
  // Step 1: the request header takes precedence when present and non-empty.
  const auto result = headers.get(header_);
  if (!result.empty()) {
    // Only the first value is used for implicitly untrusted headers.
    const absl::string_view header_value = result[0]->value().getStringView();
    if (!header_value.empty()) {
      ENVOY_LOG(trace, "header weighted cluster specifier: header {} selects cluster {}",
                header_.get(), header_value);
      return std::make_shared<Envoy::Router::DynamicRouteEntry>(std::move(parent),
                                                                std::string(header_value));
    }
  }

  // Step 2: select a cluster by weighted random choice.
  return selectClusterByWeight(std::move(parent), random);
}

Envoy::Router::RouteConstSharedPtr
HeaderWeightedClusterSpecifierPlugin::selectClusterByWeight(
    Envoy::Router::RouteEntryAndRouteConstSharedPtr parent, uint64_t random) const {
  if (total_weight_ > 0) {
    uint64_t threshold = random % total_weight_;
    for (const auto& cluster : clusters_) {
      if (threshold < cluster.weight()) {
        ENVOY_LOG(trace,
                  "header weighted cluster specifier: weighted selection picks cluster {}",
                  cluster.name());
        return std::make_shared<Envoy::Router::DynamicRouteEntry>(
            std::move(parent), std::string(cluster.name()));
      }
      threshold -= cluster.weight();
    }
  }

  ENVOY_LOG(trace, "header weighted cluster specifier: falling back to default cluster {}",
            default_cluster_);
  return std::make_shared<Envoy::Router::DynamicRouteEntry>(std::move(parent),
                                                            std::string(default_cluster_));
}

} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
