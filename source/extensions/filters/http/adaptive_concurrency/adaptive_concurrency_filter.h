#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

/**
 * Configuration for the adaptive concurrency limit filter.
 */
class AdaptiveConcurrencyFilterConfig {
public:
  AdaptiveConcurrencyFilterConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&
          adaptive_concurrency,
      Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
      TimeSource& time_source);

  Runtime::Loader& runtime() { return runtime_; }
  const std::string& statsPrefix() const { return stats_prefix_; }
  Stats::Scope& scope() { return scope_; }
  TimeSource& timeSource() { return time_source_; }

private:
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  Stats::Scope& scope_;
  TimeSource& time_source_;
};

using AdaptiveConcurrencyFilterConfigSharedPtr = std::shared_ptr<AdaptiveConcurrencyFilterConfig>;
using ConcurrencyControllerSharedPtr =
    std::shared_ptr<ConcurrencyController::ConcurrencyController>;

/**
 * A filter that samples request latencies and dynamically adjusts the request
 * concurrency window.
 */
class AdaptiveConcurrencyFilter : public Http::StreamFilter, Logger::Loggable<Logger::Id::filter> {
public:
  AdaptiveConcurrencyFilter(AdaptiveConcurrencyFilterConfigSharedPtr config,
                            ConcurrencyControllerSharedPtr controller);
  ~AdaptiveConcurrencyFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap&, bool) override;
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

private:
  AdaptiveConcurrencyFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  ConcurrencyControllerSharedPtr controller_;
  MonotonicTime rq_start_time_;
};

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
