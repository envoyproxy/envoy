#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/common/common/hash.h"
#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/tenant_id.h"
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

/**
 * @brief Helper for creating and reading the Dynatrace tag in the tracestate http header
 * This tag has at least 8 values delimited by semicolon:
 * - tag[0]: version (currently version 4)
 * - tag[1] - tag[4]: unused in the sampler (always 0)
 * - tag[5]: ignored field. 1 if a span is ignored (not sampled), 0 otherwise
 * - tag[6]: sampling exponent
 * - tag[7]: path info
 */
class DynatraceTag {
public:
  static DynatraceTag createInvalid() { return {false, false, 0, 0}; }

  // Creates a tag using the given values.
  static DynatraceTag create(bool ignored, uint32_t sampling_exponent, uint32_t path_info) {
    return {true, ignored, sampling_exponent, path_info};
  }

  // Creates a tag from a string.
  static DynatraceTag create(const std::string& value) {
    std::vector<absl::string_view> tracestate_components =
        absl::StrSplit(value, ';', absl::AllowEmpty());
    if (tracestate_components.size() < 8) {
      return createInvalid();
    }

    if (tracestate_components[0] != "fw4") {
      return createInvalid();
    }
    bool ignored = tracestate_components[5] == "1";
    uint32_t sampling_exponent;
    uint32_t path_info;
    if (absl::SimpleAtoi(tracestate_components[6], &sampling_exponent) &&
        absl::SimpleHexAtoi(tracestate_components[7], &path_info)) {
      return {true, ignored, sampling_exponent, path_info};
    }
    return createInvalid();
  }

  // Returns a Dynatrace tag as string.
  std::string asString() const {
    std::string ret = absl::StrCat("fw4;0;0;0;0;", ignored_ ? "1" : "0", ";", sampling_exponent_,
                                   ";", absl::Hex(path_info_));
    return ret;
  }

  // Returns true if parsing was successful.
  bool isValid() const { return valid_; };
  bool isIgnored() const { return ignored_; };
  uint32_t getSamplingExponent() const { return sampling_exponent_; };

private:
  DynatraceTag(bool valid, bool ignored, uint32_t sampling_exponent, uint32_t path_info)
      : valid_(valid), ignored_(ignored), sampling_exponent_(sampling_exponent),
        path_info_(path_info) {}

  bool valid_;
  bool ignored_;
  uint32_t sampling_exponent_;
  uint32_t path_info_;
};

// add Dynatrace specific span attributes
void addSamplingAttributes(uint32_t sampling_exponent, OtelAttributes& attributes) {

  const auto multiplicity = SamplingState::toMultiplicity(sampling_exponent);
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

SamplingResult DynatraceSampler::shouldSample(const absl::optional<SpanContext> parent_context,
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
      addSamplingAttributes(dynatrace_tag.getSamplingExponent(), att);
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

    addSamplingAttributes(sampling_exponent, att);

    result.decision = sample ? Decision::RecordAndSample : Decision::Drop;
    // create a new Dynatrace tag and add it to tracestate
    DynatraceTag new_tag =
        DynatraceTag::create(!sample, sampling_exponent, static_cast<uint8_t>(hash));
    trace_state = trace_state->Set(dt_tracestate_key_, new_tag.asString());
    result.tracestate = trace_state->ToHeader();
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
