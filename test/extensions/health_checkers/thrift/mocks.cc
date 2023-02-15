#include "mocks.h"

#include <cstdint>

#include "source/common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

MockClient::MockClient(ClientCallback& callback) : callback_(callback) {
  ON_CALL(*this, start()).WillByDefault(testing::Return());
  ON_CALL(*this, close()).WillByDefault(Invoke([this]() -> void {
    raiseEvent(Network::ConnectionEvent::LocalClose);
  }));
}

MockClient::~MockClient() = default;

MockClientCallback::MockClientCallback() = default;
MockClientCallback::~MockClientCallback() = default;

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
