#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::_;

namespace Envoy {
namespace ThreadLocal {

MockInstance::MockInstance() {
  ON_CALL(*this, allocateSlot()).WillByDefault(Invoke(this, &MockInstance::allocateSlot_));
  ON_CALL(*this, runOnAllThreads(_)).WillByDefault(Invoke(this, &MockInstance::runOnAllThreads_));
  ON_CALL(*this, shutdownThread()).WillByDefault(Invoke(this, &MockInstance::shutdownThread_));
}

MockInstance::~MockInstance() { shutdownThread_(); }

} // namespace ThreadLocal
} // namespace Envoy
