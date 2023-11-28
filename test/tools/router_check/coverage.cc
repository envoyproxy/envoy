#include "test/tools/router_check/coverage.h"

#include <algorithm>

namespace Envoy {
double RouteCoverage::report() {
  uint64_t route_weight = 0;
  for (auto covered_field : coverageFields()) {
    if (covered_field) {
      route_weight += 1;
    }
  }
  return static_cast<double>(route_weight) / coverageFields().size();
}

std::vector<bool> RouteCoverage::coverageFields() {
  if (route_ != nullptr) {
    return std::vector<bool>{cluster_covered_, virtual_cluster_covered_, virtual_host_covered_,
                             path_rewrite_covered_, host_rewrite_covered_};
  } else if (direct_response_entry_ != nullptr) {
    return std::vector<bool>{redirect_path_covered_, redirect_code_covered_};
  } else {
    return std::vector<bool>{};
  }
}

void Coverage::markClusterCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setClusterCovered();
}

void Coverage::markVirtualClusterCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setVirtualClusterCovered();
}

void Coverage::markVirtualHostCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setVirtualHostCovered();
}

void Coverage::markPathRewriteCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setPathRewriteCovered();
}

void Coverage::markHostRewriteCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setHostRewriteCovered();
}

void Coverage::markRedirectPathCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setRedirectPathCovered();
}

void Coverage::markRedirectCodeCovered(const Envoy::Router::RouteConstSharedPtr route) {
  coveredRoute(route).setRedirectCodeCovered();
}

double Coverage::report(bool detailed_coverage_report) {
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

  if (detailed_coverage_report) {
    for (auto& covered_route : covered_routes_) {
      covered_route_names.emplace(covered_route->routeName());
    }
    printNotCoveredRouteNames(all_route_names, covered_route_names);
  }

  return 100 * static_cast<double>(covered_routes_.size()) / num_routes;
}

double Coverage::comprehensiveReport() {
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
  double cumulative_coverage = 0;
  for (auto& covered_route : covered_routes_) {
    cumulative_coverage += covered_route->report();
    covered_route_names.emplace(covered_route->routeName());
  }
  printMissingTests(all_route_names, covered_route_names);
  return 100 * cumulative_coverage / num_routes;
}

void Coverage::printMissingTests(const std::set<std::string>& all_route_names,
                                 const std::set<std::string>& covered_route_names) {
  std::set<std::string> missing_route_names;
  std::set_difference(all_route_names.begin(), all_route_names.end(), covered_route_names.begin(),
                      covered_route_names.end(),
                      std::inserter(missing_route_names, missing_route_names.end()));

  for (const auto& host : route_config_.virtual_hosts()) {
    for (const auto& route : host.routes()) {
      if (missing_route_names.find(route.name()) != missing_route_names.end()) {
        std::string route_text;
        Protobuf::TextFormat::PrintToString(route.match(), &route_text);
        std::cout << "Missing test for host: " << host.name() << ", route: " << route_text
                  << std::endl;
      }
    }
  }
}

void Coverage::printNotCoveredRouteNames(const std::set<std::string>& all_route_names,
                                         const std::set<std::string>& covered_route_names) {
  std::set<std::string> missing_route_names;
  std::set_difference(all_route_names.begin(), all_route_names.end(), covered_route_names.begin(),
                      covered_route_names.end(),
                      std::inserter(missing_route_names, missing_route_names.end()));

  for (const auto& host : route_config_.virtual_hosts()) {
    for (const auto& route : host.routes()) {
      if (missing_route_names.find(route.name()) != missing_route_names.end()) {
        std::cout << "Missing test for host: " << host.name() << ", route name: " << route.name()
                  << std::endl;
      }
    }
  }
}

RouteCoverage& Coverage::coveredRoute(const Envoy::Router::RouteConstSharedPtr& route) {
  std::string route_name;
  if (route->routeEntry() != nullptr) {
    route_name = route->routeName();
    for (auto& route_coverage : covered_routes_) {
      if (route_coverage->covers(route)) {
        return *route_coverage;
      }
    }
    std::unique_ptr<RouteCoverage> new_coverage =
        std::make_unique<RouteCoverage>(route, route_name);
    covered_routes_.push_back(std::move(new_coverage));
    return coveredRoute(route);
  } else if (route->directResponseEntry() != nullptr) {
    const Envoy::Router::DirectResponseEntry* direct_response_entry = route->directResponseEntry();
    route_name = route->routeName();
    for (auto& route_coverage : covered_routes_) {
      if (route_coverage->covers(direct_response_entry)) {
        return *route_coverage;
      }
    }
    std::unique_ptr<RouteCoverage> new_coverage =
        std::make_unique<RouteCoverage>(direct_response_entry, route_name);
    covered_routes_.push_back(std::move(new_coverage));
    return coveredRoute(route);
  }
  PANIC("reached unexpected code");
};
} // namespace Envoy
