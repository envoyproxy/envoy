#include "all_sampler.h"

#include <sstream>
#include <string>

#include "source/common/config/datasource.h"
#include "source/extensions/tracers/opentelemetry/samplers/sampler.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

bool AllSampler::sample(SpanPtr& span) {
    (void)context_;
    (void)span;
    span->setTracestate("asdf");
    return true;
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
