#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/adaptive_concurrency/controller/controller.h"
#include "extensions/filters/http/common/pass_through_filter.h"

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
      const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency&
          proto_config,
      Runtime::Loader& runtime, std::string stats_prefix, Stats::Scope& scope,
      TimeSource& time_source);

  bool filterEnabled() const { return adaptive_concurrency_feature_.enabled(); }
  TimeSource& timeSource() const { return time_source_; }

private:
  const std::string stats_prefix_;
  TimeSource& time_source_;
  Runtime::FeatureFlag adaptive_concurrency_feature_;
};

using AdaptiveConcurrencyFilterConfigSharedPtr =
    std::shared_ptr<const AdaptiveConcurrencyFilterConfig>;
using ConcurrencyControllerSharedPtr = std::shared_ptr<Controller::ConcurrencyController>;

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
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

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
