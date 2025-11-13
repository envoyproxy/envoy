#include "source/extensions/filters/http/sampling_decision/sampling_decision_filter.h"

#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {

namespace {
constexpr const char* DefaultMetadataNamespace = "envoy.filters.http.sampling_decision";
constexpr const char* SampledKey = "sampled";
constexpr const char* NumeratorKey = "numerator";
constexpr const char* DenominatorKey = "denominator";
constexpr const char* RuntimeKeyFieldName = "runtime_key";
} // namespace

SamplingDecisionConfig::SamplingDecisionConfig(
    const envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision& config)
    : runtime_key_(config.runtime_key()),
      percent_sampled_(config.has_percent_sampled() ? config.percent_sampled()
                                                    : envoy::type::v3::FractionalPercent()),
      use_independent_randomness_(config.use_independent_randomness()),
      metadata_namespace_(config.metadata_namespace().empty() ? DefaultMetadataNamespace
                                                              : config.metadata_namespace()) {}

SamplingDecisionFilter::SamplingDecisionFilter(SamplingDecisionConfigSharedPtr config,
                                               Runtime::Loader& runtime,
                                               Random::RandomGenerator& random)
    : config_(std::move(config)), runtime_(runtime), random_(random) {}

bool SamplingDecisionFilter::evaluateSamplingDecision() {
  // This code is verbose to avoid pre-allocating a random number that is not needed.
  uint64_t random_value;
  if (config_->useIndependentRandomness()) {
    random_value = random_.random();
  } else if (!decoder_callbacks_->streamInfo().getStreamIdProvider().has_value()) {
    random_value = random_.random();
  } else {
    const auto rid_to_integer = decoder_callbacks_->streamInfo().getStreamIdProvider()->toInteger();
    if (!rid_to_integer.has_value()) {
      random_value = random_.random();
    } else {
      random_value =
          rid_to_integer.value() % ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                                       config_->percentSampled().denominator());
    }
  }

  const uint64_t denominator = ProtobufPercentHelper::fractionalPercentDenominatorToInt(
      config_->percentSampled().denominator());

  return runtime_.snapshot().featureEnabled(
      config_->runtimeKey(), config_->percentSampled().numerator(), random_value, denominator);
}

Http::FilterHeadersStatus SamplingDecisionFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // Evaluate the sampling decision.
  const bool sampled = evaluateSamplingDecision();

  // Get the actual numerator from runtime (may differ from config default).
  const uint64_t numerator =
      runtime_.snapshot().getInteger(config_->runtimeKey(), config_->percentSampled().numerator());
  const uint64_t denominator = ProtobufPercentHelper::fractionalPercentDenominatorToInt(
      config_->percentSampled().denominator());

  // Store the sampling decision and parameters in dynamic metadata.
  Protobuf::Struct sampling_metadata;
  auto* sampling_fields = sampling_metadata.mutable_fields();

  (*sampling_fields)[SampledKey].set_bool_value(sampled);
  (*sampling_fields)[RuntimeKeyFieldName].set_string_value(config_->runtimeKey());
  (*sampling_fields)[NumeratorKey].set_number_value(numerator);
  (*sampling_fields)[DenominatorKey].set_number_value(denominator);

  decoder_callbacks_->streamInfo().setDynamicMetadata(config_->metadataNamespace(),
                                                      sampling_metadata);

  ENVOY_LOG(trace,
            "Sampling decision for request: sampled={}, runtime_key={}, numerator={}, "
            "denominator={}",
            sampled, config_->runtimeKey(), numerator, denominator);

  return Http::FilterHeadersStatus::Continue;
}

} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
