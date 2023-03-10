#include "envoy/thread/thread.h"
#include "source/server/guarddog_impl.h"

#pragma once

namespace Envoy {
namespace Server {

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

