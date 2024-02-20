#include "source/extensions/tracers/opentelemetry/samplers/always_on/always_on_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplingResult AlwaysOnSampler::shouldSample(const absl::optional<SpanContext> parent_context,
                                             const std::string& /*trace_id*/,
                                             const std::string& /*name*/, OTelSpanKind /*kind*/,
                                             OptRef<const Tracing::TraceContext> /*trace_context*/,
                                             const std::vector<SpanContext>& /*links*/) {
  SamplingResult result;
  result.decision = Decision::RecordAndSample;
  if (parent_context.has_value()) {
    result.tracestate = parent_context.value().tracestate();
  }
  return result;
}

std::string AlwaysOnSampler::getDescription() const { return "AlwaysOnSampler"; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
