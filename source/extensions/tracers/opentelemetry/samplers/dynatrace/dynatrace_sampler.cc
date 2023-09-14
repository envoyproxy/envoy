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

SamplingResult
DynatraceSampler::shouldSample(absl::StatusOr<SpanContext>& parent_context,
                              const std::string& trace_id, const std::string& name,
                              ::opentelemetry::proto::trace::v1::Span::SpanKind spankind, 
                              const std::map<std::string, std::string>& attributes) {
  (void)trace_id;
  (void)name;
  (void)spankind;
  (void)attributes;
  SamplingResult result;

  if (counter_ %2 == 0) {
    result.decision = Decision::RECORD_AND_SAMPLE;
    result.trace_state = "tracestate_set_in_dynatrace_samplerX";
    std::map<std::string, std::string> att;
    att["key_set_in_dynatrace_sampler"] = "value_set_in_dynatrace_sampler";
    result.attributes = std::make_unique<const std::map<std::string, std::string>>(std::move(att));
  } else {
    result.decision = Decision::DROP;
    if (parent_context.ok()) {
      result.trace_state = parent_context.value().tracestate();
    }
  }
  ++counter_;
  return result;
}

std::string DynatraceSampler::getDescription() const {
    return "DynatraceSampler";
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
