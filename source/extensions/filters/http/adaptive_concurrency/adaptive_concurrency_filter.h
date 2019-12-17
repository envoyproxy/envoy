#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/runtime/runtime_features.h"

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

/**
 * All stats for the adaptive concurrency filter.
 */
#define ALL_ADAPTIVE_CONCURRENCY_STATS(GAUGE) GAUGE(enabled, NeverImport)

/**
 * Wrapper struct for the adaptive concurrency stats. @see stats_macros.h
 */
struct AdaptiveConcurrencyStats {
  ALL_ADAPTIVE_CONCURRENCY_STATS(GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the adaptive concurrency limit filter.
 */
class AdaptiveConcurrencyFilterConfig {
public:
  AdaptiveConcurrencyFilterConfig(
      const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&
          proto_config,
      Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
      TimeSource& time_source);

  bool filterEnabled() const {
    const bool enabled = adaptive_concurrency_feature_.enabled();
    stats_.enabled_.set(enabled);
    return enabled;
  }

  TimeSource& timeSource() const { return time_source_; }

private:
  static AdaptiveConcurrencyStats generateStats(Stats::Scope& scope,
                                                const std::string& stats_prefix);

  const std::string stats_prefix_;
  TimeSource& time_source_;
  Runtime::FeatureFlag adaptive_concurrency_feature_;
  Stats::Scope& scope_;
  AdaptiveConcurrencyStats stats_;
};

using AdaptiveConcurrencyFilterConfigSharedPtr =
    std::shared_ptr<const AdaptiveConcurrencyFilterConfig>;
using ConcurrencyControllerSharedPtr =
    std::shared_ptr<ConcurrencyController::ConcurrencyController>;

/**
 * A filter that samples request latencies and dynamically adjusts the request
 * concurrency window.
 */
class AdaptiveConcurrencyFilter : public Http::PassThroughFilter,
                                  Logger::Loggable<Logger::Id::filter> {
public:
  AdaptiveConcurrencyFilter(AdaptiveConcurrencyFilterConfigSharedPtr config,
                            ConcurrencyControllerSharedPtr controller);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  void encodeComplete() override;
  void onDestroy() override;

private:
  AdaptiveConcurrencyFilterConfigSharedPtr config_;
  const ConcurrencyControllerSharedPtr controller_;
  std::unique_ptr<Cleanup> deferred_sample_task_;
};

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
