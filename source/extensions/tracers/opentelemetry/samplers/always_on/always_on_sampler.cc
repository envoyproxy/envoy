#include "always_on_sampler.h"

#include <memory>
#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplingResult AlwaysOnSampler::shouldSample(absl::StatusOr<SpanContext> &parent_context, std::string trace_id, std::string name, ::opentelemetry::proto::trace::v1::Span::SpanKind spankind) {
    (void)parent_context;
    (void)context_;
    (void)trace_id;
    (void)name;
    (void)spankind;
    SamplingResult result;
    result.decision = Decision::RECORD_AND_SAMPLE;
    // ----
    result.trace_state = "FW_FW_FW";
    std::map<std::string, std::string> att;
    att["this_is_my_key"] = "this_is_my_value";
    result.attributes = std::make_unique<const std::map<std::string, std::string>> (std::move(att));
    // ----
    return result;
}

std::string AlwaysOnSampler::getDescription() const {
    return "AlwaysOnSampler";
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
