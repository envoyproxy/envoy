#pragma once

#include "envoy/thread/thread.h"

#include "source/server/guarddog_impl.h"

#pragma once

namespace Envoy {
namespace Server {

// Helps make tests using the GuardDog more robust by providing a way of
// blocking in the test on GuardDog loop completion when
// GuardDogImpl::forceCheckForTest() is called. If this interlock is not
// provided, the default interlock is a no-op.
class GuardDogTestInterlock : public GuardDogImpl::TestInterlockHook {
public:
  // GuardDogImpl::TestInterlockHook
  void signalFromImpl() override {
    waiting_for_signal_ = false;
    impl_.notifyAll();
  }

  void waitFromTest(Thread::MutexBasicLockable& mutex) override
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    ASSERT(!waiting_for_signal_);
    waiting_for_signal_ = true;
    while (waiting_for_signal_) {
      impl_.wait(mutex);
    }
  }

private:
  Thread::CondVar impl_;
  bool waiting_for_signal_ = false;
};

} // namespace Server
} // namespace Envoy
