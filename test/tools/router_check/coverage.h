#pragma once

#include "envoy/router/router.h"

#include "test/mocks/server/mocks.h"

namespace Envoy {
class Coverage : Logger::Loggable<Logger::Id::testing> {
public:
  Coverage(envoy::api::v2::RouteConfiguration config) : route_config_(config){};
  void markCovered(const Envoy::Router::RouteEntry& route);
  double report();

private:
  std::vector<const Envoy::Router::RouteEntry*> seen_routes_;
  const envoy::api::v2::RouteConfiguration route_config_;
};
} // namespace Envoy
