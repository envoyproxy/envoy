#include "dynatrace_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "tracestate.h"
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
  // uint32_t current_counter = counter_++;
  TraceState tracestate;

  if (parent_context.ok()) { // there is already a trace,
    // we should check if there is a dynatrace sampling decision on the state and use it
    result.decision = Decision::RECORD_AND_SAMPLE;
    // Expects a tracestate like
    // "<tenantID>-<clusterID>@dt=fw4;0;0;0;0;<isIgnored>;8;<rootPathRandom>;<extensionChecksum>"
    tracestate = TraceState::parse(parent_context->tracestate());
  } else { // start new trace
    // if (current_counter % 2 == 0) {
      result.decision = Decision::RECORD_AND_SAMPLE;
      tracestate.sampling_exponent = "8"; // "8" is used for demo. Will be received from configuration in a final version
    // } else {
    //   result.decision = Decision::RECORD_ONLY;
    //   if (parent_context.ok()) {
    //     result.tracestate = parent_context.value().tracestate();
    //   }
    // }
  }

  tracestate.tenant_id = tenant_id_;
  tracestate.cluster_id = cluster_id_;
  tracestate.is_ignored = result.isRecording() ? "0" : "1";
  att[SAMPLING_EXTRAPOLATION_SPAN_ATTRIBUTE_NAME] = tracestate.sampling_exponent;
  result.tracestate = tracestate.toString();
  if (!att.empty()) {
    result.attributes = std::make_unique<const std::map<std::string, std::string>>(std::move(att));
  }
  return result;
}

std::string DynatraceSampler::getDescription() const { return "DynatraceSampler"; }

std::string DynatraceSampler::modifyTracestate(const std::string& span_id,
                                               const std::string& current_tracestate) const {
  
  TraceState tracestate = TraceState::parse(current_tracestate);
  tracestate.span_id = span_id;
  tracestate.tenant_id = tenant_id_;
  tracestate.cluster_id = cluster_id_;
  return tracestate.toString();
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
