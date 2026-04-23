#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() {
  // Default to recording so existing tests that don't stub isRecording() see the same
  // behavior as the Tracing::Span base class (which returns true). Tests that want to
  // exercise the non-recording HCM skip path can override this via EXPECT_CALL/ON_CALL.
  ON_CALL(*this, isRecording()).WillByDefault(Return(true));
}
MockSpan::~MockSpan() = default;

MockConfig::MockConfig() {
  ON_CALL(*this, operationName()).WillByDefault(ReturnPointee(&operation_name_));
  ON_CALL(*this, verbose()).WillByDefault(ReturnPointee(&verbose_));
  ON_CALL(*this, maxPathTagLength()).WillByDefault(Return(uint32_t(256)));
  ON_CALL(*this, spawnUpstreamSpan()).WillByDefault(ReturnPointee(&spawn_upstream_span_));
  ON_CALL(*this, noContextPropagation()).WillByDefault(ReturnPointee(&no_context_propagation_));
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
