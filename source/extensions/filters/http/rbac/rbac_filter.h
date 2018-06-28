#pragma once

#include <memory>

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

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

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(EnforcementMode mode) const {
    return mode == EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

private:
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> engine_;
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> shadow_engine_;
};

/**
 * Configuration for the RBAC filter.
 */
class RoleBasedAccessControlFilterConfig {
public:
  RoleBasedAccessControlFilterConfig(
      const envoy::config::filter::http::rbac::v2::RBAC& proto_config,
      const std::string& stats_prefix, Stats::Scope& scope);

  RoleBasedAccessControlFilterStats& stats() { return stats_; }

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(const Router::RouteConstSharedPtr route, EnforcementMode mode) const;

private:
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(EnforcementMode mode) const {
    return mode == EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

  RoleBasedAccessControlFilterStats stats_;

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> engine_;
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl> shadow_engine_;
};

typedef std::shared_ptr<RoleBasedAccessControlFilterConfig>
    RoleBasedAccessControlFilterConfigSharedPtr;

/**
 * A filter that provides role-based access control authorization for HTTP requests.
 */
class RoleBasedAccessControlFilter : public Http::StreamDecoderFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {
public:
  RoleBasedAccessControlFilter(RoleBasedAccessControlFilterConfigSharedPtr config)
      : config_(config) {}

  static RoleBasedAccessControlFilterStats generateStats(const std::string& prefix,
                                                         Stats::Scope& scope);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;

  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override {}

private:
  RoleBasedAccessControlFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
