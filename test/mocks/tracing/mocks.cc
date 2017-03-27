#include "mocks.h"

using testing::ReturnRef;

namespace Tracing {

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(ReturnRef(operation_name_));
}
MockConfig::~MockConfig() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockDriver::MockDriver() {}
MockDriver::~MockDriver() {}

} // Tracing