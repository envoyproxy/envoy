#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

MockCancellable::MockCancellable() {}
MockCancellable::~MockCancellable() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
