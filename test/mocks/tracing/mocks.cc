#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() = default;
MockSpan::~MockSpan() = default;

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(ReturnPointee(&operation_name_));
  ON_CALL(*this, customTags()).WillByDefault(Return(&custom_tags_));
  ON_CALL(*this, verbose()).WillByDefault(ReturnPointee(&verbose_));
  ON_CALL(*this, maxPathTagLength()).WillByDefault(Return(uint32_t(256)));
  ON_CALL(*this, spawnUpstreamSpan()).WillByDefault(ReturnPointee(&spawn_upstream_span_));
}
MockConfig::~MockConfig() = default;

MockTracer::MockTracer() = default;
MockTracer::~MockTracer() = default;

MockDriver::MockDriver() = default;
MockDriver::~MockDriver() = default;

MockTracerManager::MockTracerManager() = default;
MockTracerManager::~MockTracerManager() = default;

} // namespace Tracing
} // namespace Envoy
