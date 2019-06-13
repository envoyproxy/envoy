#pragma once

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/filter/http/csrf/v2/csrf.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

/**
 * All CSRF filter stats. @see stats_macros.h
 */
// clang-format off
#define ALL_CSRF_STATS(COUNTER) \
  COUNTER(missing_source_origin)\
  COUNTER(request_invalid)      \
  COUNTER(request_valid)        \
// clang-format on

/**
 * Struct definition for CSRF stats. @see stats_macros.h
 */
struct CsrfStats {
  ALL_CSRF_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for CSRF policy.
 */
class CsrfPolicy : public Router::RouteSpecificFilterConfig {
public:
  CsrfPolicy(const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
             Runtime::Loader& runtime) : policy_(policy), runtime_(runtime) {}

  bool enabled() const {
    const envoy::api::v2::core::RuntimeFractionalPercent& filter_enabled = policy_.filter_enabled();
    return runtime_.snapshot().featureEnabled(filter_enabled.runtime_key(),
                                              filter_enabled.default_value());
  }

  bool shadowEnabled() const {
    if (!policy_.has_shadow_enabled()) {
      return false;
    }
    const envoy::api::v2::core::RuntimeFractionalPercent& shadow_enabled = policy_.shadow_enabled();
    return runtime_.snapshot().featureEnabled(shadow_enabled.runtime_key(),
                                              shadow_enabled.default_value());
  }

private:
  const envoy::config::filter::http::csrf::v2::CsrfPolicy policy_;
  Runtime::Loader& runtime_;
};

/**
 * Configuration for the CSRF filter.
 */
class CsrfFilterConfig {
public:
  CsrfFilterConfig(const envoy::config::filter::http::csrf::v2::CsrfPolicy& policy,
                   const std::string& stats_prefix, Stats::Scope& scope,
                   Runtime::Loader& runtime);

  CsrfStats& stats() { return stats_; }
  const CsrfPolicy* policy() { return &policy_; }

private:
  CsrfStats stats_;
  const CsrfPolicy policy_;
};
using CsrfFilterConfigSharedPtr = std::shared_ptr<CsrfFilterConfig>;

class CsrfFilter : public Http::StreamDecoderFilter {
public:
  CsrfFilter(CsrfFilterConfigSharedPtr config);

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  };
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  };
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  };

private:
  void determinePolicy();

  Http::StreamDecoderFilterCallbacks* callbacks_{};
  CsrfFilterConfigSharedPtr config_;
  const CsrfPolicy* policy_;
};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
