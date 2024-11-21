#pragma once

#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * All CORS filter stats. @see stats_macros.h
 */
#define ALL_CORS_STATS(COUNTER)                                                                    \
  COUNTER(origin_valid)                                                                            \
  COUNTER(origin_invalid)

/**
 * Struct definition for CORS stats. @see stats_macros.h
 */
struct CorsStats {
  ALL_CORS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the CORS filter.
 */
class CorsFilterConfig {
public:
  CorsFilterConfig(const std::string& stats_prefix, Stats::Scope& scope);
  CorsStats& stats() { return stats_; }

private:
  static CorsStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return CorsStats{ALL_CORS_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  CorsStats stats_;
};
using CorsFilterConfigSharedPtr = std::shared_ptr<CorsFilterConfig>;

class CorsFilter : public Envoy::Http::PassThroughFilter {
public:
  CorsFilter(CorsFilterConfigSharedPtr config);

  void initializeCorsPolicies();

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;

  const auto& policiesForTest() const { return policies_; }

private:
  friend class CorsFilterTest;

  absl::Span<const Matchers::StringMatcherPtr> allowOrigins();
  absl::string_view allowMethods();
  absl::string_view allowHeaders();
  absl::string_view exposeHeaders();
  absl::string_view maxAge();
  bool allowCredentials();
  bool allowPrivateNetworkAccess();
  bool shadowEnabled();
  bool enabled();
  bool isOriginAllowed(const Http::HeaderString& origin);
  bool forwardNotMatchingPreflights();

  absl::InlinedVector<std::reference_wrapper<const Envoy::Router::CorsPolicy>, 4> policies_;
  bool is_cors_request_{};
  std::string latched_origin_;

  CorsFilterConfigSharedPtr config_;
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
