#include "always_on_sampler.h"

#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

SamplingResult AlwaysOnSampler::shouldSample() {
    (void)context_;
    SamplingResult result;
    result.decision = Decision::RECORD_AND_SAMPLE;
    return {};
}

std::string AlwaysOnSampler::getDescription() const {
    return "AlwaysOnSampler";
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
