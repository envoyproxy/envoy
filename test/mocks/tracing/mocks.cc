#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnPointee;

namespace Envoy {
namespace Tracing {

MockSpan::MockSpan() {
  ON_CALL(*this, exportedSpan()).WillByDefault(Return(true));
  // Mirror the production Span::setTypedTag default, which records the value via setTag(). This
  // keeps expectations written against setTag() working for tags that do not exercise
  // typed-attribute behavior.
  ON_CALL(*this, setTypedTag(_, _, _))
      .WillByDefault([this](absl::string_view name, absl::string_view value, TagValueType) {
        setTag(name, value);
      });
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
