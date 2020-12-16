#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/bandwidth_limit/v3alpha/bandwidth_limit.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/assert.h"
#include "common/common/token_bucket_impl.h"
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
#define ALL_BANDWIDTH_LIMIT_STATS(COUNTER)                                                         \
  COUNTER(enabled)                                                                                 \
  COUNTER(enforced)                                                                                \
  COUNTER(bandwidth_limited)                                                                       \
  COUNTER(bandwidth_usage)                                                                         \
  COUNTER(ok)

/**
 * Struct definition for all bandwidth limit stats. @see stats_macros.h
 */
struct BandwidthLimitStats {
  ALL_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for the HTTP bandwidth limit filter.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  using EnableMode =
      envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit_EnableMode;

  static constexpr uint64_t MaxFillRate = 32;

  FilterConfig(
      const envoy::extensions::filters::http::bandwidth_limit::v3alpha::BandwidthLimit& config,
      Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source,
      bool per_route = false);
  ~FilterConfig() override = default;
  Runtime::Loader& runtime() { return runtime_; }
  BandwidthLimitStats& stats() const { return stats_; }
  Stats::Scope& scope() { return scope_; }
  TimeSource& timeSource() { return time_source_; }
  // Must call enabled() before calling limit().
  uint64_t limit() const { return limit_kbps_; }
  EnableMode enable_mode() const { return enable_mode_; };
  std::shared_ptr<TokenBucketImpl> tokenBucket() { return token_bucket_; }
  const std::shared_ptr<TokenBucketImpl> tokenBucket() const { return token_bucket_; }
  uint64_t fill_rate() const { return fill_rate_; }

private:
  friend class FilterTest;

  static BandwidthLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  mutable BandwidthLimitStats stats_;
  Runtime::Loader& runtime_;
  Stats::Scope& scope_;
  TimeSource& time_source_;
  const uint64_t limit_kbps_;
  const EnableMode enable_mode_;
  const uint64_t fill_rate_;
  // Filter chain's shared token bucket
  std::shared_ptr<TokenBucketImpl> token_bucket_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP bandwidth limit filter. Depending on the route configuration, this filter calls consults
 * with local token bucket before allowing further filter iteration.
 */
class BandwidthLimiter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  BandwidthLimiter(FilterConfigSharedPtr config) : config_(config) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;

  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }

  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  // Http::StreamFilterBase
  void onDestroy() override;

private:
  friend class FilterTest;
  const FilterConfig* getConfig() const;

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  FilterConfigSharedPtr config_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> ingress_limiter_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> egress_limiter_;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
