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
    //Expects a tracestate like "FW4;129;12;-2023406815;4539717;5555;17;66;c511;2h02;3h12345678;4h676767"
    std::vector<absl::string_view> tracestate_components =
        absl::StrSplit(parent_context->tracestate(), ';', absl::AllowEmpty());
    if (tracestate_components.size() > 6) {
      att[SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME] = tracestate_components[5];
    }
    result.trace_state = parent_context.value().tracestate();
  } else { // start new trace

    if (current_counter % 2 == 0) {
      result.decision = Decision::RECORD_AND_SAMPLE;
      result.trace_state = "FW4;129;12;-2023406815;4539717;1000;17;66;c511;2h02;3h12345678;4h676767";
      att[SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME] = "1000";
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

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
