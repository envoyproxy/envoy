#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/safe_memcpy.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace {
/**
 * Converts a ratio in [0, 1] to a threshold in [0, MILLION].
 */
uint64_t ratioToThreshold(double ratio) noexcept {
  const uint64_t MAX_VALUE = Envoy::ProtobufPercentHelper::fractionalPercentDenominatorToInt(
      envoy::type::v3::FractionalPercent::MILLION);

  if (ratio <= 0.0) {
    return 0;
  }
  if (ratio >= 1.0) {
    return MAX_VALUE;
  }

  return static_cast<uint64_t>(ratio * static_cast<double>(MAX_VALUE));
}

/**
 * @param trace_id a required value to be converted to uint64_t. trace_id must
 * at least 8 bytes long. trace_id is expected to be a valid hex string.
 * @return Returns the uint64 value associated with first 8 bytes of the trace_id modulo MILLION.
 *
 */
uint64_t calculateThresholdFromBuffer(const std::string& trace_id) noexcept {
  uint8_t buffer[8] = {0};
  for (size_t i = 0; i < 8; ++i) {
    std::string byte_string = trace_id.substr(i * 2, 2);
    buffer[i] = static_cast<uint8_t>(std::stoul(byte_string, nullptr, 16));
  }

  uint64_t first_8_bytes = 0;
  Envoy::safeMemcpyUnsafeSrc(&first_8_bytes, buffer);

  return first_8_bytes % Envoy::ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                             envoy::type::v3::FractionalPercent::MILLION);
}
} // namespace

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TraceIdRatioBasedSampler::TraceIdRatioBasedSampler(
    const envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig&
        config,
    Server::Configuration::TracerFactoryContext& /*context*/)
    : threshold_(ratioToThreshold(config.ratio())) {
  double ratio = config.ratio();
  if (ratio > 1.0) {
    ratio = 1.0;
  }
  if (ratio < 0.0) {
    ratio = 0.0;
  }
  description_ = "TraceIdRatioBasedSampler{" + std::to_string(ratio) + "}";
}

SamplingResult TraceIdRatioBasedSampler::shouldSample(
    const StreamInfo::StreamInfo&, const absl::optional<SpanContext> parent_context,
    const std::string& trace_id, const std::string& /*name*/, OTelSpanKind /*kind*/,
    OptRef<const Tracing::TraceContext> /*trace_context*/,
    const std::vector<SpanContext>& /*links*/) {
  SamplingResult result;
  result.decision = Decision::Drop;
  if (parent_context.has_value()) {
    result.tracestate = parent_context.value().tracestate();
  }

  if (threshold_ == 0) {
    return result;
  }

  if (calculateThresholdFromBuffer(trace_id) <= threshold_) {
    result.decision = Decision::RecordAndSample;
    return result;
  }

  return result;
}

std::string TraceIdRatioBasedSampler::getDescription() const { return description_; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
