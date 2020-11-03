#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/bandwidth_limit/v3/bandwidth_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/router/header_parser.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/common/stream_rate_limiter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

/**
 * All bandwidth limit stats. @see stats_macros.h
 */
#define ALL_BANDWIDTH_LIMIT_STATS(COUNTER)                                                      \
  COUNTER(enabled)                                                                              \
  COUNTER(enforced)                                                                             \
  COUNTER(bandwidth_limited)                                                                    \
  COUNTER(bandwidth_usage)                                                                      \
  COUNTER(ok)

/**
 * Struct definition for all bandwidth limit stats. @see stats_macros.h
 */
struct BandwidthLimitStats {
  ALL_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for the HTTP bandwidth limit filter.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& config,
               Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
               bool per_route = false);
  ~FilterConfig() override = default;
  Runtime::Loader& runtime() { return runtime_; }
  bool requestAllowed() const;
  bool enabled() const;
  bool enforced() const;
  BandwidthLimitStats& stats() const { return stats_; }
  const Router::HeaderParser& responseHeadersParser() const { return *response_headers_parser_; }

private:
  friend class FilterTest;

  static BandwidthLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  mutable BandwidthLimitStats stats_;
  Filters::Common::BandwidthLimit::BandwidthLimiterImpl rate_limiter_;
  Runtime::Loader& runtime_;
  Filters::BandwidthLimit::Enable_Mode enable_mode_;
  const absl::optional<Envoy::Runtime::FractionalPercent> filter_enabled_;
  Router::HeaderParserPtr response_headers_parser_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP bandwidth limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class BandwidthFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  BandwidthFilter(FilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  friend class FilterTest;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> stream_limiter_;
  const FilterConfig* getConfig() const;

  FilterConfigSharedPtr config_;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
