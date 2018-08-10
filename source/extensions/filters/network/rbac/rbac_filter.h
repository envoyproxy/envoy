#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/common/rbac/engine_impl.h"
#include "extensions/filters/common/rbac/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RBACFilter {

enum EngineResult { Unknown, None, Allow, Deny };

/**
 * Configuration for the RBAC network filter.
 */
class RoleBasedAccessControlFilterConfig {
public:
  RoleBasedAccessControlFilterConfig(
      const envoy::config::filter::network::rbac::v2::RBAC& proto_config, Stats::Scope& scope);

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats& stats() { return stats_; }

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

private:
  Filters::Common::RBAC::RoleBasedAccessControlFilterStats stats_;

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> engine_;
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> shadow_engine_;
};

typedef std::shared_ptr<RoleBasedAccessControlFilterConfig>
    RoleBasedAccessControlFilterConfigSharedPtr;

/**
 * Implementation of a basic RBAC network filter.
 */
class RoleBasedAccessControlFilter : public Network::ReadFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {
public:
  RoleBasedAccessControlFilter(RoleBasedAccessControlFilterConfigSharedPtr config)
      : config_(config) {}
  ~RoleBasedAccessControlFilter() {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; };
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

private:
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  Network::ReadFilterCallbacks* callbacks_{};
  EngineResult engine_result_{Unknown};
  EngineResult shadow_engine_result_{Unknown};

  EngineResult checkEngine(Filters::Common::RBAC::EnforcementMode mode);
};

} // namespace RBACFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
