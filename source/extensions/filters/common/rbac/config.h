#pragma once

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/config/filter/network/rbac/v2/rbac.pb.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

/**
 * All stats for the RBAC filter. @see stats_macros.h
 */
// clang-format off
#define ALL_RBAC_FILTER_STATS(COUNTER)                                                             \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)                                                                                  \
  COUNTER(shadow_allowed)                                                                          \
  COUNTER(shadow_denied)
// clang-format on

/**
 * Wrapper struct for RBAC filter stats. @see stats_macros.h
 */
struct RoleBasedAccessControlFilterStats {
  ALL_RBAC_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

enum class EnforcementMode { Enforced, Shadow };

class RoleBasedAccessControlRouteSpecificFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  RoleBasedAccessControlRouteSpecificFilterConfig(
      const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config);

  const absl::optional<RoleBasedAccessControlEngineImpl>& engine(EnforcementMode mode) const {
    return mode == EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

private:
  const absl::optional<RoleBasedAccessControlEngineImpl> engine_;
  const absl::optional<RoleBasedAccessControlEngineImpl> shadow_engine_;
};

/**
 * Configuration for the RBAC filter.
 */
class RoleBasedAccessControlFilterConfig {
public:
  // Constructor for RBAC http filter.
  RoleBasedAccessControlFilterConfig(
      const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope);

  // Constructor for RBAC network filter, header and metadata in proto_config will always match.
  RoleBasedAccessControlFilterConfig(
      const envoy::config::filter::network::rbac::v2::RBAC& proto_config, Stats::Scope& scope);

  static RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                         Stats::Scope& scope);

  RoleBasedAccessControlFilterStats& stats() { return stats_; }

  const absl::optional<RoleBasedAccessControlEngineImpl>&
  engine(const Router::RouteConstSharedPtr route, const std::string& filter_name,
         EnforcementMode mode) const;

private:
  const absl::optional<RoleBasedAccessControlEngineImpl>& engine(EnforcementMode mode) const {
    return mode == EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

  RoleBasedAccessControlFilterStats stats_;

  const absl::optional<RoleBasedAccessControlEngineImpl> engine_;
  const absl::optional<RoleBasedAccessControlEngineImpl> shadow_engine_;
};

typedef std::shared_ptr<RoleBasedAccessControlFilterConfig>
    RoleBasedAccessControlFilterConfigSharedPtr;

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
