#include "test/tools/router_check/coverage.h"

#include <algorithm>

#include "envoy/api/v2/core/base.pb.h"

namespace Envoy {
double RouteCoverage::report() {
  uint64_t route_weight = 0;
  for (const auto& covered_field : coverageFields()) {
    if (covered_field) {
      route_weight += 1;
    }
  }
  return static_cast<double>(route_weight) / coverageFields().size();
}

void Coverage::markClusterCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setClusterCovered();
}

void Coverage::markVirtualClusterCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setVirtualClusterCovered();
}

void Coverage::markVirtualHostCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setVirtualHostCovered();
}

void Coverage::markPathRewriteCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setPathRewriteCovered();
}

void Coverage::markHostRewriteCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setHostRewriteCovered();
}

void Coverage::markRedirectPathCovered(const Envoy::Router::Route& route) {
  coveredRoute(route).setRedirectPathCovered();
}

double Coverage::report(bool detailed) {
  std::set<std::string> all_route_names;
  std::set<std::string> covered_route_names;
  uint64_t num_routes = 0;
  for (const auto& host : route_config_.virtual_hosts()) {
    for (const auto& route : host.routes()) {
      if (route.route().has_weighted_clusters()) {
        num_routes += route.route().weighted_clusters().clusters_size();
      } else {
        num_routes += 1;
      }
      all_route_names.emplace(route.name());
    }
  }
  for (auto& covered_route : covered_routes_) {
    covered_route_names.emplace(covered_route->routeName());
  }
  std::set<std::string> missing_route_names;
  std::set_difference(all_route_names.begin(), all_route_names.end(), covered_route_names.begin(),
                      covered_route_names.end(),
                      std::inserter(missing_route_names, missing_route_names.end()));
  if (detailed) {
    printMissingTests(missing_route_names);
    double cumulative_coverage = getCumulativeCoverage(all_route_names, missing_route_names);
    return 100 * cumulative_coverage / num_routes;
  } else {
    return 100 * static_cast<double>(num_routes - missing_route_names.size()) / num_routes;
  }
}

void Coverage::printMissingTests(const std::set<std::string>& missing_route_names) {
  for (const auto& host : route_config_.virtual_hosts()) {
    for (const auto& route : host.routes()) {
      if (missing_route_names.find(route.name()) != missing_route_names.end()) {
        std::cout << "Missing test for host: " << host.name()
                  << ", route: " << route.match().DebugString() << std::endl;
      }
    }
  }
}

double Coverage::getCumulativeCoverage(const std::set<std::string>& all_route_names,
                                       const std::set<std::string>& missing_route_names) {
  std::set<std::string> defined_covered_route_names;
  std::set_difference(
      all_route_names.begin(), all_route_names.end(), missing_route_names.begin(),
      missing_route_names.end(),
      std::inserter(defined_covered_route_names, defined_covered_route_names.end()));
  double cumulative_coverage = 0;
  for (auto& covered_route : covered_routes_) {
    if (defined_covered_route_names.find(covered_route->routeName()) !=
        defined_covered_route_names.end()) {
      cumulative_coverage += covered_route->report();
    }
  }
  return cumulative_coverage;
}

RouteCoverage& Coverage::coveredRoute(const Envoy::Router::Route& route) {
  const Envoy::Router::ResponseEntry* response_entry;
  std::string route_name;
  if (route.routeEntry() != nullptr) {
    response_entry = route.routeEntry();
    route_name = route.routeEntry()->routeName();
  } else if (route.directResponseEntry() != nullptr) {
    response_entry = route.directResponseEntry();
    route_name = route.directResponseEntry()->routeName();
  }
  for (auto& route_coverage : covered_routes_) {
    if (route_coverage->covers(response_entry)) {
      return *route_coverage;
    }
  }
  std::unique_ptr<RouteCoverage> new_coverage = std::make_unique<RouteCoverage>(response_entry, route_name);
  covered_routes_.push_back(std::move(new_coverage));
  return coveredRoute(route);
};
} // namespace Envoy
