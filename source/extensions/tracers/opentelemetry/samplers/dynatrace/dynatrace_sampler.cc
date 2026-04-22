#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h"

#include <chrono>
#include <memory>
#include <string>

#include "source/common/common/hash.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_tag.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/tenant_id.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/trace_capture_reason.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"
#include "source/extensions/tracers/opentelemetry/span_context.h"

#include "absl/strings/str_cat.h"
#include "opentelemetry/trace/trace_state.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace {

constexpr std::chrono::minutes SAMPLING_UPDATE_TIMER_DURATION{1};

// add Dynatrace specific span attributes
void addSamplingAttributes(OtelAttributes& attributes, const DynatraceTag& dynatrace_tag) {

  const auto multiplicity = SamplingState::toMultiplicity(dynatrace_tag.getSamplingExponent());
  // The denominator of the sampling ratio. If, for example, the Dynatrace OneAgent samples with a
  // probability of 1/16, the value of supportability sampling ratio would be 16.
  // Note: Ratio is also known as multiplicity.
  attributes["supportability.atm_sampling_ratio"] = multiplicity;

  if (multiplicity > 1) {
    static constexpr uint64_t two_pow_56 = 1llu << 56; // 2^56
    // The sampling probability can be interpreted as the number of spans
    // that are discarded out of 2^56. The attribute is only available if the sampling.threshold is
    // not 0 and therefore sampling happened.
    const uint64_t sampling_threshold = two_pow_56 - two_pow_56 / multiplicity;
    attributes["sampling.threshold"] = sampling_threshold;
  }

  auto tcr = dynatrace_tag.getTcrExtension();

  if (tcr && tcr->isValid()) {
    auto span_attribute_value = tcr->toSpanAttributeValue();
    attributes["trace.capture.reasons"] = span_attribute_value;
  }
}

} // namespace

DynatraceSampler::DynatraceSampler(
    const envoy::extensions::tracers::opentelemetry::samplers::v3::DynatraceSamplerConfig& config,
    Server::Configuration::TracerFactoryContext& context,
    SamplerConfigProviderPtr sampler_config_provider)
    : dt_tracestate_key_(absl::StrCat(calculateTenantId(config.tenant()), "-",
                                      absl::Hex(config.cluster_id()), "@dt")),
      sampling_controller_(std::move(sampler_config_provider)) {

  // start a timer to periodically recalculate the sampling exponents
  timer_ = context.serverFactoryContext().mainThreadDispatcher().createTimer([this]() -> void {
    sampling_controller_.update();
    timer_->enableTimer(SAMPLING_UPDATE_TIMER_DURATION);
  });
  timer_->enableTimer(SAMPLING_UPDATE_TIMER_DURATION);
}

SamplingResult DynatraceSampler::shouldSample(const StreamInfo::StreamInfo&,
                                              const absl::optional<SpanContext> parent_context,
                                              const std::string& trace_id,
                                              const std::string& /*name*/, OTelSpanKind /*kind*/,
                                              OptRef<const Tracing::TraceContext> trace_context,
                                              const std::vector<SpanContext>& /*links*/) {

  SamplingResult result;
  OtelAttributes att;

  // trace_context->path() returns path and query. query part is removed in getSamplingKey()
  const std::string sampling_key =
      trace_context.has_value()
          ? sampling_controller_.getSamplingKey(trace_context->path(), trace_context->method())
          : "";

  sampling_controller_.offer(sampling_key);

  auto trace_state = opentelemetry::trace::TraceState::FromHeader(
      parent_context.has_value() ? parent_context->tracestate() : "");

  std::string trace_state_value;
  bool is_root_span = true;

  if (trace_state->Get(dt_tracestate_key_, trace_state_value)) {
    // we found a Dynatrace tag in the tracestate header. Respect the sampling decision in the tag.
    if (DynatraceTag dynatrace_tag = DynatraceTag::create(trace_state_value);
        dynatrace_tag.isValid()) {
      result.decision = dynatrace_tag.isIgnored() ? Decision::Drop : Decision::RecordAndSample;
      addSamplingAttributes(att, dynatrace_tag);
      result.tracestate = parent_context->tracestate();
      is_root_span = false;
    }
  }

  if (is_root_span) {
    // do a decision based on the calculated exponent
    // we use a hash of the trace_id as random number
    const auto hash = MurmurHash::murmurHash2(trace_id);
    const auto sampling_state = sampling_controller_.getSamplingState(sampling_key);
    const bool sample = sampling_state.shouldSample(hash);
    const auto sampling_exponent = sampling_state.getExponent();

    result.decision = sample ? Decision::RecordAndSample : Decision::Drop;

    // create a new Dynatrace tag and add it to tracestate
    DynatraceTag new_tag =
        DynatraceTag::create(!sample, sampling_exponent, static_cast<uint8_t>(hash),
                             TraceCaptureReason::create(TraceCaptureReason::Reason::Atm));

    trace_state = trace_state->Set(dt_tracestate_key_, new_tag.asString());
    result.tracestate = trace_state->ToHeader();

    addSamplingAttributes(att, new_tag);
  }

  if (!att.empty()) {
    result.attributes = std::make_unique<const OtelAttributes>(std::move(att));
  }

  return result;
}

std::string DynatraceSampler::getDescription() const { return "DynatraceSampler"; }

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
