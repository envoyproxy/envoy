#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/common/rbac/config.h"
#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

enum EngineResult { UNKNOWN, NONE, ALLOW, DENY };

/**
 * Implementation of a basic RBAC network filter.
 */
class RoleBasedAccessControlFilter : public Network::ReadFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {
public:
  RoleBasedAccessControlFilter(
      Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config)
      : config_(config) {}
  ~RoleBasedAccessControlFilter() {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; };
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

private:
  Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* callbacks_{};
  EngineResult engine_result_{UNKNOWN};
  EngineResult shadow_engine_result_{UNKNOWN};

  EngineResult checkEngine(Filters::Common::RBAC::EnforcementMode mode);
};

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
