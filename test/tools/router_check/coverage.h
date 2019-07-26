#pragma once

#include <memory>
#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/router/router.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/stats/fake_symbol_table_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

namespace Envoy {
class Coverage : Logger::Loggable<Logger::Id::testing> {
public:
  Coverage(envoy::api::v2::RouteConfiguration config) : route_config_(config){};
  void markCovered(const Envoy::Router::RouteEntry* route);
  double report();

private:
  std::vector<const Envoy::Router::RouteEntry*> seen_routes_;
  envoy::api::v2::RouteConfiguration route_config_;
};
} // namespace Envoy
