#include "mocks.h"

namespace Tracing {

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockTracingConfig::MockTracingConfig() {}
MockTracingConfig::~MockTracingConfig() {}

MockTracingContext::MockTracingContext() {}
MockTracingContext::~MockTracingContext() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockTracingDriver::MockTracingDriver() {}
MockTracingDriver::~MockTracingDriver() {}

} // Tracing