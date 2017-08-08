#include "common/http/filter_utility.h"

namespace Envoy {
namespace Http {

Upstream::ClusterInfoConstSharedPtr
FilterUtility::resolveClusterInfo(Http::StreamDecoderFilterCallbacks* decoder_callbacks,
                                  Upstream::ClusterManager& cm) {
  Router::RouteConstSharedPtr route = decoder_callbacks->route();
  if (!route || !route->routeEntry()) {
    return nullptr;
  }

  const Router::RouteEntry* route_entry = route->routeEntry();
  Upstream::ThreadLocalCluster* cluster = cm.get(route_entry->clusterName());
  if (!cluster) {
    return nullptr;
  }
  return cluster->info();
}

} // namespace Http
} // namespace Envoy
