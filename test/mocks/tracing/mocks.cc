#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() = default;
MockSpan::~MockSpan() = default;

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_));
  ON_CALL(*this, requestHeadersForTags()).WillByDefault(ReturnRef(headers_));
  ON_CALL(*this, verbose()).WillByDefault(Return(verbose_));
}
MockConfig::~MockConfig() = default;

MockHttpTracer::MockHttpTracer() = default;
MockHttpTracer::~MockHttpTracer() = default;

MockDriver::MockDriver() = default;
MockDriver::~MockDriver() = default;

} // namespace Tracing
} // namespace Envoy
