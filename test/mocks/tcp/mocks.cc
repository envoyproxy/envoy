#include "mocks.h"

#include "gmock/gmock.h"

using testing::ReturnRef;

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

MockCancellable::MockCancellable() {}
MockCancellable::~MockCancellable() {}

MockUpstreamCallbacks::MockUpstreamCallbacks() {}
MockUpstreamCallbacks::~MockUpstreamCallbacks() {}

MockConnectionData::MockConnectionData() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}
MockConnectionData::~MockConnectionData() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
