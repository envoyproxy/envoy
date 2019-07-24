#pragma once

#include <memory>

#include "envoy/config/filter/http/abac/v2alpha/abac.pb.h"
#include "envoy/http/filter.h"

//#include "envoy/stats/scope.h"
//#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "eval/public/cel_expression.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ABACFilter {

/**
 * A filter that provides attribute-based access control authorization for HTTP requests.
 */
class AttributeBasedAccessControlFilter : public Http::StreamDecoderFilter,
                                          public Logger::Loggable<Logger::Id::abac> {
public:
  AttributeBasedAccessControlFilter(std::shared_ptr<google::api::expr::runtime::CelExpression> expr)
      : expr_(expr) {}

  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override;

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
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  std::shared_ptr<google::api::expr::runtime::CelExpression> expr_;
};

} // namespace ABACFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
