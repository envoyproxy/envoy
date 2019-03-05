#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <string>

#include "envoy/thread/thread.h"

#include "common/common/assert.h"

#include "absl/synchronization/notification.h"

namespace quic {

// A class representing a thread of execution in QUIC.
class QuicThreadImpl {
public:
  QuicThreadImpl(const std::string& /*name*/) {}
  QuicThreadImpl(const QuicThreadImpl&) = delete;
  QuicThreadImpl& operator=(const QuicThreadImpl&) = delete;

  virtual ~QuicThreadImpl() {
    if (thread_ != nullptr) {
      PANIC("QuicThread should be joined before destruction.");
    }
  }

  void Start() {
    if (thread_ != nullptr || thread_is_set_.HasBeenNotified()) {
      PANIC("QuicThread can only be started once.");
    }
    thread_ = Envoy::Thread::ThreadFactorySingleton::get().createThread([this]() {
      thread_is_set_.WaitForNotification();
      this->Run();
    });
    thread_is_set_.Notify();
  }

  void Join() {
    if (thread_ == nullptr) {
      PANIC("QuicThread has not been started.");
    }
    thread_->join();
    thread_ = nullptr;
  }

protected:
  virtual void Run() = 0;

private:
  Envoy::Thread::ThreadPtr thread_;
  absl::Notification thread_is_set_; // Whether |thread_| is set in parent.
};

} // namespace quic
