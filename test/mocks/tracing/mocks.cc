#include "mocks.h"

#include "gmock/gmock.h"
#include "test/test_common/test_base.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() {}
MockSpan::~MockSpan() {}

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_));
  ON_CALL(*this, requestHeadersForTags()).WillByDefault(ReturnRef(headers_));
}
MockConfig::~MockConfig() {}

MockHttpTracer::MockHttpTracer() {}
MockHttpTracer::~MockHttpTracer() {}

MockDriver::MockDriver() {}
MockDriver::~MockDriver() {}

} // namespace Tracing
} // namespace Envoy
