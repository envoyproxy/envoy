#include "dynatrace_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

static const char* SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME =
    "sampling_extrapolation_set_in_sampler";
SamplingResult
DynatraceSampler::shouldSample(absl::StatusOr<SpanContext>& parent_context,
                               const std::string& /*trace_id*/, const std::string& /*name*/,
                               ::opentelemetry::proto::trace::v1::Span::SpanKind /*spankind*/,
                               const std::map<std::string, std::string>& /*attributes*/,
                               const std::set<SpanContext> /*links*/) {

  SamplingResult result;
  std::map<std::string, std::string> att;
  uint32_t current_counter = counter_++;

  if (parent_context.ok()) { // there is already a trace,
    result.decision = Decision::RECORD_AND_SAMPLE;
    // Expects a tracestate like
    // "<tenantID>-<clusterID>@dt=fw4;0;0;0;0;<isIgnored>;8;<rootPathRandom>;<extensionChecksum>"
    std::vector<absl::string_view> tracestate_components =
        absl::StrSplit(parent_context->tracestate(), ';', absl::AllowEmpty());
    if (tracestate_components.size() > 7) {
      att[SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME] = tracestate_components[6];
    }
    result.trace_state = parent_context.value().tracestate();
  } else { // start new trace

    if (current_counter % 2 == 0) {
      result.decision = Decision::RECORD_AND_SAMPLE;
      result.trace_state = "<tenantID>-<clusterID>@dt=fw4;0;0;0;0;<isIgnored>;8;<rootPathRandom>;<extensionChecksum>";
      att[SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME] = "8";
    } else {
      result.decision = Decision::RECORD_ONLY;
      if (parent_context.ok()) {
        result.trace_state = parent_context.value().tracestate();
      }
    }
  }
  if (att.size()) {
    result.attributes = std::make_unique<const std::map<std::string, std::string>>(std::move(att));
  }
  return result;
}

std::string DynatraceSampler::getDescription() const { return "DynatraceSampler"; }

std::string DynatraceSampler::modifyTraceState(const std::string& span_id,
                                               const std::string& current_trace_state) const {
  (void)span_id;
  (void)current_trace_state;
  std::string new_trace_state(current_trace_state);
  new_trace_state.append(";7h");
  new_trace_state.append(span_id);
  return new_trace_state;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
