#pragma once

#include <memory>

#include "envoy/config/filter/http/rbac/v2/rbac.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "extensions/filters/common/rbac/config.h"
#include "extensions/filters/common/rbac/engine_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RBACFilter {

/**
 * A filter that provides role-based access control authorization for HTTP requests.
 */
class RoleBasedAccessControlFilter : public Http::StreamDecoderFilter,
                                     public Logger::Loggable<Logger::Id::rbac> {
public:
  RoleBasedAccessControlFilter(
      Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config)
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
  Filters::Common::RBAC::RoleBasedAccessControlFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace RBACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
