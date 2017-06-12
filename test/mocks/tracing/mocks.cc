#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Return;
using testing::ReturnRef;

namespace Tracing {

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockFinalizer::MockFinalizer() {}
MockFinalizer::~MockFinalizer() {}

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_));
  ON_CALL(*this, requestHeadersForTags()).WillByDefault(ReturnRef(headers_));
}
MockConfig::~MockConfig() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockDriver::MockDriver() {}
MockDriver::~MockDriver() {}

} // Tracing
} // Envoy
