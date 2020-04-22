#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace ThreadLocal {

MockInstance::MockInstance() {
  ON_CALL(*this, allocateSlot()).WillByDefault(Invoke(this, &MockInstance::allocateSlot_));
  ON_CALL(*this, runOnAllThreads(_)).WillByDefault(Invoke(this, &MockInstance::runOnAllThreads1_));
  ON_CALL(*this, runOnAllThreads(_, _))
      .WillByDefault(Invoke(this, &MockInstance::runOnAllThreads2_));
  ON_CALL(*this, shutdownThread()).WillByDefault(Invoke(this, &MockInstance::shutdownThread_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}

MockInstance::~MockInstance() { shutdownThread_(); }

} // namespace ThreadLocal
} // namespace Envoy
