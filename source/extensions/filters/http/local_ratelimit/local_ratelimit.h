#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/router/header_parser.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

/**
 * All local rate limit stats. @see stats_macros.h
 */
#define ALL_LOCAL_RATE_LIMIT_STATS(COUNTER)                                                        \
  COUNTER(enabled)                                                                                 \
  COUNTER(enforced)                                                                                \
  COUNTER(rate_limited)                                                                            \
  COUNTER(ok)

/**
 * Struct definition for all local rate limit stats. @see stats_macros.h
 */
struct LocalRateLimitStats {
  ALL_LOCAL_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for the HTTP local rate limit filter.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
               Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
               bool per_route = false);
  ~FilterConfig() override = default;
  Runtime::Loader& runtime() { return runtime_; }
  bool requestAllowed() const;
  bool enabled() const;
  bool enforced() const;
  LocalRateLimitStats& stats() const { return stats_; }
  const Router::HeaderParser& responseHeadersParser() const { return *response_headers_parser_; }
  Http::Code status() const { return status_; }

private:
  friend class FilterTest;

  static LocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  static Http::Code toErrorCode(uint64_t status) {
    const auto code = static_cast<Http::Code>(status);
    if (code >= Http::Code::BadRequest) {
      return code;
    }
    return Http::Code::TooManyRequests;
  }

  const Http::Code status_;
  mutable LocalRateLimitStats stats_;
  Filters::Common::LocalRateLimit::LocalRateLimiterImpl rate_limiter_;
  Runtime::Loader& runtime_;
  const absl::optional<Envoy::Runtime::FractionalPercent> filter_enabled_;
  const absl::optional<Envoy::Runtime::FractionalPercent> filter_enforced_;
  Router::HeaderParserPtr response_headers_parser_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP local rate limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class Filter : public Http::PassThroughFilter {
public:
  Filter(FilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  friend class FilterTest;

  const FilterConfig* getConfig() const;

  FilterConfigSharedPtr config_;
};

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
