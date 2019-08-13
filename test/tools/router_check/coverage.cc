#include "test/tools/router_check/coverage.h"

#include <algorithm>

#include "envoy/api/v2/core/base.pb.h"

namespace Envoy {
void Coverage::markCovered(const Envoy::Router::RouteEntry& route) {
  // n.b. If we reach the end of the seen routes without finding the specified
  // route we add it as seen, otherwise it's a duplicate.
  if (std::find(seen_routes_.begin(), seen_routes_.end(), &route) == seen_routes_.end()) {
    seen_routes_.push_back(&route);
  }
}

double Coverage::report() {
  uint64_t num_routes = 0;
  for (const auto& host : route_config_.virtual_hosts()) {
    num_routes += host.routes_size();
  }
  return 100 * static_cast<double>(seen_routes_.size()) / num_routes;
}
} // namespace Envoy
