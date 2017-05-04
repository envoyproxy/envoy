#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace ThreadLocal {

MockInstance::MockInstance() {
  ON_CALL(*this, allocateSlot()).WillByDefault(Invoke(this, &MockInstance::allocateSlot_));
  ON_CALL(*this, get(_)).WillByDefault(Invoke(this, &MockInstance::get_));
  ON_CALL(*this, runOnAllThreads(_)).WillByDefault(Invoke(this, &MockInstance::runOnAllThreads_));
  ON_CALL(*this, set(_, _)).WillByDefault(Invoke(this, &MockInstance::set_));
  ON_CALL(*this, shutdownThread()).WillByDefault(Invoke(this, &MockInstance::shutdownThread_));
}

MockInstance::~MockInstance() {}

} // ThreadLocal
