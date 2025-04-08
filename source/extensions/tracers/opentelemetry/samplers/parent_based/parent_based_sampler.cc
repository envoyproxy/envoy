#include "source/extensions/tracers/opentelemetry/samplers/parent_based/parent_based_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplingResult ParentBasedSampler::shouldSample(const StreamInfo::StreamInfo& stream_info,
                                                const absl::optional<SpanContext> parent_context,
                                                const std::string& trace_id,
                                                const std::string& name, OTelSpanKind kind,
                                                OptRef<const Tracing::TraceContext> trace_context,
                                                const std::vector<SpanContext>& links) {
  if (!parent_context.has_value() || parent_context->traceId().empty()) {
    return wrapped_sampler_->shouldSample(stream_info, parent_context, trace_id, name, kind,
                                          trace_context, links);
  }

  SamplingResult result;
  result.tracestate = parent_context.value().tracestate();
  if (parent_context->sampled()) {
    result.decision = Decision::RecordAndSample;
  } else {
    result.decision = Decision::Drop;
  }
  return result;
}

std::string ParentBasedSampler::getDescription() const { return "ParentBasedSampler"; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
