#include "test/tools/router_check/coverage.h"

#include <algorithm>

#include "envoy/api/v2/core/base.pb.h"

namespace Envoy {
void Coverage::markCovered(const Envoy::Router::RouteEntry* route) {
  bool seen = std::find(seen_routes_.begin(), seen_routes_.end(), route) != seen_routes_.end();
  if (!seen) {
    seen_routes_.push_back(route);
  }
}

double Coverage::report() {
  int size_t = 0;
  for (const auto& host : route_config_.virtual_hosts()) {
    size_t += host.routes_size();
  }
  return static_cast<double>(seen_routes_.size()) / size_t;
}
} // namespace Envoy
