#include "source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/logger.h"
#include "source/common/common/safe_memcpy.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * @param trace_id a required value to be converted to uint64_t. trace_id must
 * at least 8 bytes long. trace_id is expected to be a valid hex string.
 * @return Returns the uint64 value associated with first 8 bytes of the trace_id.
 *
 */
uint64_t TraceIdRatioBasedSampler::traceIdToUint64(const std::string& trace_id) noexcept {
  if (trace_id.size() < 16) {
    ENVOY_LOG(warn, "Trace ID is not long enough: {}", trace_id);
    return 0;
  }

  uint8_t buffer[8] = {0};
  for (size_t i = 0; i < 8; ++i) {
    std::string byte_string = trace_id.substr(i * 2, 2);
    buffer[i] = static_cast<uint8_t>(std::stoul(byte_string, nullptr, 16));
  }

  uint64_t first_8_bytes = 0;
  Envoy::safeMemcpyUnsafeSrc(&first_8_bytes, buffer);

  return first_8_bytes;
}

TraceIdRatioBasedSampler::TraceIdRatioBasedSampler(
    const envoy::extensions::tracers::opentelemetry::samplers::v3::TraceIdRatioBasedSamplerConfig&
        config,
    Server::Configuration::TracerFactoryContext& /*context*/)
    : sampling_percentage_(config.sampling_percentage()) {
  const envoy::type::v3::FractionalPercent& sampling_percentage = config.sampling_percentage();
  description_ = "TraceIdRatioBasedSampler{" + std::to_string(sampling_percentage.numerator()) +
                 "/" +
                 std::to_string(ProtobufPercentHelper::fractionalPercentDenominatorToInt(
                     sampling_percentage.denominator())) +
                 "}";
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

  if (sampling_percentage_.numerator() == 0) {
    return result;
  }

  if (ProtobufPercentHelper::evaluateFractionalPercent(sampling_percentage_,
                                                       traceIdToUint64(trace_id))) {
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
