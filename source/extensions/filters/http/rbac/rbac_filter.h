#pragma once

#include <memory>

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/common/rbac/engine_impl.h"
#include "extensions/filters/common/rbac/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

class RoleBasedAccessControlRouteSpecificFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  RoleBasedAccessControlRouteSpecificFilterConfig(
      const envoy::config::filter::http::rbac::v2::RBACPerRoute& per_route_config);

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_ : shadow_engine_;
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

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats& stats() { return stats_; }

  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(const Router::RouteConstSharedPtr route,
         Filters::Common::RBAC::EnforcementMode mode) const;

private:
  const absl::optional<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>&
  engine(Filters::Common::RBAC::EnforcementMode mode) const {
    return mode == Filters::Common::RBAC::EnforcementMode::Enforced ? engine_ : shadow_engine_;
  }

  Filters::Common::RBAC::RoleBasedAccessControlFilterStats stats_;

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
