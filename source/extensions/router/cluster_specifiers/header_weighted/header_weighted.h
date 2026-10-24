#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "envoy/extensions/router/cluster_specifiers/header_weighted/v3/header_weighted.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/router/cluster_specifier_plugin.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Router {
namespace HeaderWeighted {

using HeaderWeightedConfigProto =
    envoy::extensions::router::cluster_specifiers::header_weighted::v3::HeaderWeighted;

/**
 * HeaderWeightedClusterSpecifierPlugin selects the upstream cluster in two steps:
 * 1. If the configured request header is present and non-empty, its value is
 *    used as the target cluster name.
 * 2. Otherwise, a cluster is selected from the configured cluster list by
 *    weighted random choice. If the total weight is zero, the default cluster
 *    is used.
 */
class HeaderWeightedClusterSpecifierPlugin : public Envoy::Router::ClusterSpecifierPlugin,
                                             Logger::Loggable<Logger::Id::router> {
public:
  explicit HeaderWeightedClusterSpecifierPlugin(
      const HeaderWeightedConfigProto& config);

  absl::Status
  validateClusters(const Upstream::ClusterManager& cm) const override;

  Envoy::Router::RouteConstSharedPtr
  route(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
         const Http::RequestHeaderMap& headers, const StreamInfo::StreamInfo& stream_info,
         uint64_t random) const override;

private:
  Envoy::Router::RouteConstSharedPtr
  selectClusterByWeight(Envoy::Router::RouteEntryAndRouteConstSharedPtr parent,
                        uint64_t random) const;

  const Http::LowerCaseString header_;
  const std::vector<HeaderWeightedConfigProto::ClusterWeight> clusters_;
  const uint64_t total_weight_;
  const std::string default_cluster_;
};

} // namespace HeaderWeighted
} // namespace Router
} // namespace Extensions
} // namespace Envoy
