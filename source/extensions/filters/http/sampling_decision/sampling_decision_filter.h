#pragma once

#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"
#include "envoy/runtime/runtime.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {

/**
 * Configuration for the sampling decision filter.
 */
class SamplingDecisionConfig {
public:
  SamplingDecisionConfig(
      const envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision& config);

  const std::string& runtimeKey() const { return runtime_key_; }
  const envoy::type::v3::FractionalPercent& percentSampled() const { return percent_sampled_; }
  bool useIndependentRandomness() const { return use_independent_randomness_; }
  const std::string& metadataNamespace() const { return metadata_namespace_; }

private:
  const std::string runtime_key_;
  const envoy::type::v3::FractionalPercent percent_sampled_;
  const bool use_independent_randomness_;
  const std::string metadata_namespace_;
};

using SamplingDecisionConfigSharedPtr = std::shared_ptr<SamplingDecisionConfig>;

/**
 * HTTP filter that evaluates sampling decisions and stores them in dynamic metadata.
 *
 * This filter evaluates whether a request should be sampled based on runtime configuration
 * and stores the decision in dynamic metadata. The metadata can then be consumed by other
 * filters such as access log filters to determine whether to log the request.
 */
class SamplingDecisionFilter : public Http::PassThroughDecoderFilter,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  SamplingDecisionFilter(SamplingDecisionConfigSharedPtr config, Runtime::Loader& runtime,
                         Random::RandomGenerator& random);

  // Http::StreamFilterBase.
  void onDestroy() override {}

  // Http::StreamDecoderFilter.
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  // Evaluate the sampling decision and return whether the request should be sampled.
  bool evaluateSamplingDecision();

  const SamplingDecisionConfigSharedPtr config_;
  Runtime::Loader& runtime_;
  Random::RandomGenerator& random_;
};

} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
