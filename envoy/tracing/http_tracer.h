#include "envoy/tracing/tracer.h"

namespace Envoy {
namespace Tracing {

// Alias name of Tracer to reduce unnecessary changes in the current code.
using HttpTracer = Tracer;
using HttpTracerSharedPtr = std::shared_ptr<HttpTracer>;

} // namespace Tracing
} // namespace Envoy
