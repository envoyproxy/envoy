#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "source/common/common/safe_memcpy.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace {
/**
 * Converts a ratio in [0, 1] to a threshold in [0, UINT64_MAX].
 *
 * Adapted from https://github.com/open-telemetry/opentelemetry-cpp
 */
uint64_t ratioToThreshold(double ratio) noexcept {
  if (ratio <= 0.0) {
    return 0;
  }
  if (ratio >= 1.0) {
    return UINT64_MAX;
  }

  // We can't directly return ratio * UINT64_MAX because of precision issues with doubles.
  //
  // UINT64_MAX is (2^64 - 1), but a double has limited precision and rounds up when
  // converting large numbers. For probabilities >= 1 - (2^-54), multiplying by UINT64_MAX
  // can cause overflow and the result wraps around to zero!
  // We know (UINT64_MAX)*ratio = (UINT32_MAX*2^32 + UINT32_MAX)*ratio. So, To avoid this,
  // we calculate the high and low 32 bits separately by using the double's precision to
  // get the most accurate result.
  // Then we add the the high and low bits to get the result.
  const double product = UINT32_MAX * ratio;
  double hi_bits;
  double lo_bits = modf(product, &hi_bits);
  lo_bits = ldexp(lo_bits, 32) + product;
  return (static_cast<uint64_t>(hi_bits) << 32) + static_cast<uint64_t>(lo_bits);
}

/**
 * @param trace_id a required value to be converted to uint64_t. trace_id must
 * at least 8 bytes long. trace_id is expected to be a valid hex string.
 * @return Returns the uint64 value associated with first 8 bytes of the trace_id.
 *
 * Adapted from https://github.com/open-telemetry/opentelemetry-cpp
 */
uint64_t calculateThresholdFromBuffer(const std::string& trace_id) noexcept {
  uint8_t buffer[8] = {0};
  for (size_t i = 0; i < 8; ++i) {
    std::string byte_string = trace_id.substr(i * 2, 2);
    buffer[i] = static_cast<uint8_t>(std::stoul(byte_string, nullptr, 16));
  }

  uint64_t first_8_bytes = 0;
  Envoy::safeMemcpyUnsafeSrc(&first_8_bytes, buffer);
  double ratio = static_cast<double>(first_8_bytes) / static_cast<double>(UINT64_MAX);

  return ratioToThreshold(ratio);
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

SamplingResult
TraceIdRatioBasedSampler::shouldSample(const absl::optional<SpanContext> parent_context,
                                       const std::string& trace_id, const std::string& /*name*/,
                                       OTelSpanKind /*kind*/,
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
