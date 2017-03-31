#include "mocks.h"

using testing::Return;

namespace Tracing {

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockConfig::MockConfig() { ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_)); }
MockConfig::~MockConfig() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockDriver::MockDriver() {}
MockDriver::~MockDriver() {}

} // Tracing