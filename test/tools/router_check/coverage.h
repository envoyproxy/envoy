#pragma once

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/router/router.h"

#include "test/mocks/server/mocks.h"

namespace Envoy {
class RouteCoverage : Logger::Loggable<Logger::Id::testing> {
public:
  RouteCoverage(const Envoy::Router::RouteEntry* route, const std::string route_name)
      : route_entry_(route), direct_response_entry_(nullptr), route_name_(route_name){};
  RouteCoverage(const Envoy::Router::DirectResponseEntry* route, const std::string route_name)
      : route_entry_(nullptr), direct_response_entry_(route), route_name_(route_name){};

  double report();
  void setClusterCovered() { cluster_covered_ = true; }
  void setVirtualClusterCovered() { virtual_cluster_covered_ = true; }
  void setVirtualHostCovered() { virtual_host_covered_ = true; }
  void setPathRewriteCovered() { path_rewrite_covered_ = true; }
  void setHostRewriteCovered() { host_rewrite_covered_ = true; }
  void setRedirectPathCovered() { redirect_path_covered_ = true; }
  bool covers(const Envoy::Router::RouteEntry* route) { return route_entry_ == route; }
  bool covers(const Envoy::Router::DirectResponseEntry* route) {
    return direct_response_entry_ == route;
  }
  const std::string routeName() { return route_name_; };

private:
  const Envoy::Router::RouteEntry* route_entry_;
  const Envoy::Router::DirectResponseEntry* direct_response_entry_;
  const std::string route_name_;
  bool cluster_covered_{false};
  bool virtual_cluster_covered_{false};
  bool virtual_host_covered_{false};
  bool path_rewrite_covered_{false};
  bool host_rewrite_covered_{false};
  bool redirect_path_covered_{false};
  std::vector<bool> coverageFields();
};

class Coverage : Logger::Loggable<Logger::Id::testing> {
public:
  Coverage(envoy::config::route::v3::RouteConfiguration config) : route_config_(config){};
  void markClusterCovered(const Envoy::Router::Route& route);
  void markVirtualClusterCovered(const Envoy::Router::Route& route);
  void markVirtualHostCovered(const Envoy::Router::Route& route);
  void markPathRewriteCovered(const Envoy::Router::Route& route);
  void markHostRewriteCovered(const Envoy::Router::Route& route);
  void markRedirectPathCovered(const Envoy::Router::Route& route);
  double report();
  double detailedReport();

private:
  RouteCoverage& coveredRoute(const Envoy::Router::Route& route);
  void printMissingTests(const std::set<std::string>& all_route_names,
                         const std::set<std::string>& covered_route_names);
  std::vector<std::unique_ptr<RouteCoverage>> covered_routes_;
  const envoy::config::route::v3::RouteConfiguration route_config_;
};
} // namespace Envoy
