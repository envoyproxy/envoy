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
#include "envoy/stats/timespan.h"

#include "source/common/common/assert.h"
#include "source/common/common/shared_token_bucket_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"
#include "source/common/runtime/runtime_protos.h"
#include "source/extensions/filters/http/common/stream_rate_limiter.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

/**
 * All bandwidth limit stats. @see stats_macros.h
 */
#define ALL_BANDWIDTH_LIMIT_STATS(COUNTER, GAUGE, HISTOGRAM)                                       \
  COUNTER(request_enabled)                                                                         \
  COUNTER(response_enabled)                                                                        \
  COUNTER(request_enforced)                                                                        \
  COUNTER(response_enforced)                                                                       \
  GAUGE(request_pending, Accumulate)                                                               \
  GAUGE(response_pending, Accumulate)                                                              \
  GAUGE(request_incoming_size, Accumulate)                                                         \
  GAUGE(response_incoming_size, Accumulate)                                                        \
  GAUGE(request_allowed_size, Accumulate)                                                          \
  GAUGE(response_allowed_size, Accumulate)                                                         \
  COUNTER(request_incoming_total_size)                                                             \
  COUNTER(response_incoming_total_size)                                                            \
  COUNTER(request_allowed_total_size)                                                              \
  COUNTER(response_allowed_total_size)                                                             \
  HISTOGRAM(request_transfer_duration, Milliseconds)                                               \
  HISTOGRAM(response_transfer_duration, Milliseconds)

/**
 * Struct definition for all bandwidth limit stats. @see stats_macros.h
 */
struct BandwidthLimitStats {
  ALL_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                            GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Configuration for the HTTP bandwidth limit filter.
 */
class FilterConfig : public ::Envoy::Router::RouteSpecificFilterConfig {
public:
  using EnableMode =
      envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit_EnableMode;

  FilterConfig(const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& config,
               Stats::Scope& scope, Runtime::Loader& runtime, TimeSource& time_source,
               bool per_route = false);
  ~FilterConfig() override = default;
  Runtime::Loader& runtime() { return runtime_; }
  BandwidthLimitStats& stats() const { return stats_; }
  TimeSource& timeSource() { return time_source_; }
  // Must call enabled() before calling limit().
  uint64_t limit() const { return limit_kbps_; }
  bool enabled() const { return enabled_.enabled(); }
  EnableMode enableMode() const { return enable_mode_; };
  const std::shared_ptr<SharedTokenBucketImpl> tokenBucket() const { return token_bucket_; }
  std::chrono::milliseconds fillInterval() const { return fill_interval_; }
  const Http::LowerCaseString& requestDelayTrailer() const { return request_delay_trailer_; }
  const Http::LowerCaseString& responseDelayTrailer() const { return response_delay_trailer_; }
  const Http::LowerCaseString& requestFilterDelayTrailer() const {
    return request_filter_delay_trailer_;
  }
  const Http::LowerCaseString& responseFilterDelayTrailer() const {
    return response_filter_delay_trailer_;
  }
  bool enableResponseTrailers() const { return enable_response_trailers_; }

private:
  friend class FilterTest;

  static BandwidthLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);

  Runtime::Loader& runtime_;
  TimeSource& time_source_;
  const EnableMode enable_mode_;
  const uint64_t limit_kbps_;
  const std::chrono::milliseconds fill_interval_;
  const Runtime::FeatureFlag enabled_;
  mutable BandwidthLimitStats stats_;
  // Filter chain's shared token bucket
  std::shared_ptr<SharedTokenBucketImpl> token_bucket_;
  const Http::LowerCaseString request_delay_trailer_;
  const Http::LowerCaseString response_delay_trailer_;
  const Http::LowerCaseString request_filter_delay_trailer_;
  const Http::LowerCaseString response_filter_delay_trailer_;
  const bool enable_response_trailers_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * HTTP bandwidth limit filter. Depending on the route configuration, this
 * filter calls consults with local token bucket before allowing further filter
 * iteration.
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
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
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
  const FilterConfig& getConfig() const;
  const std::chrono::milliseconds zero_milliseconds_ = std::chrono::milliseconds(0);

  void updateStatsOnDecodeFinish();
  void updateStatsOnEncodeFinish();

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  FilterConfigSharedPtr config_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> request_limiter_;
  std::unique_ptr<Envoy::Extensions::HttpFilters::Common::StreamRateLimiter> response_limiter_;
  Stats::TimespanPtr request_latency_;
  Stats::TimespanPtr response_latency_;
  std::chrono::milliseconds request_duration_ = zero_milliseconds_;
  std::chrono::milliseconds request_delay_ = zero_milliseconds_;
  std::chrono::milliseconds response_delay_ = zero_milliseconds_;
  Http::ResponseTrailerMap* trailers_;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
