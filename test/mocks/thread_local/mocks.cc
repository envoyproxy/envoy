#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace ThreadLocal {

MockInstance::MockInstance() {
  ON_CALL(*this, allocateSlot()).WillByDefault(Invoke(this, &MockInstance::allocateSlotMock));
  ON_CALL(*this, runOnAllThreads(_)).WillByDefault(Invoke(this, &MockInstance::runOnAllThreads1));
  ON_CALL(*this, runOnAllThreads(_, _))
      .WillByDefault(Invoke(this, &MockInstance::runOnAllThreads2));
  ON_CALL(*this, shutdownThread()).WillByDefault(Invoke(this, &MockInstance::shutdownThread_));
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(*dispatcher_ptr_));
}

MockInstance::~MockInstance() { shutdownThread_(); }

} // namespace ThreadLocal
} // namespace Envoy
