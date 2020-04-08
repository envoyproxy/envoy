#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() = default;
MockSpan::~MockSpan() = default;

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(Return(operation_name_));
  ON_CALL(*this, customTags()).WillByDefault(Return(&custom_tags_));
  ON_CALL(*this, verbose()).WillByDefault(Return(verbose_));
  ON_CALL(*this, maxPathTagLength()).WillByDefault(Return(uint32_t(256)));
}
MockConfig::~MockConfig() = default;

MockHttpTracer::MockHttpTracer() = default;
MockHttpTracer::~MockHttpTracer() = default;

MockDriver::MockDriver() = default;
MockDriver::~MockDriver() = default;

MockHttpTracerManager::MockHttpTracerManager() = default;
MockHttpTracerManager::~MockHttpTracerManager() = default;

} // namespace Tracing
} // namespace Envoy
