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
    ASSERT(thread_factory_ != nullptr);
    if (thread_ != nullptr || thread_is_set_.HasBeenNotified()) {
      PANIC("QuicThread can only be started once.");
    }
    thread_ = thread_factory_->createThread([this]() {
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

  // Sets the thread factory to use.
  // NOTE: The factory can not be passed via a constructor argument because this class is itself a
  // dependency of an external library that derives from it and expects a single argument
  // constructor.
  void setThreadFactory(Envoy::Thread::ThreadFactory& thread_factory) {
    thread_factory_ = &thread_factory;
  }

protected:
  virtual void Run() {
    // We don't want this function to be pure virtual, because it will be called if:
    // 1. An object of a derived class calls Start(), which starts the child thread
    // but has not called Run() yet.
    // 2. The destructor of the derived class is called, but not the destructor
    // of this base class.
    // 3. The child thread calls QuicThreadImpl::Run()(this function), since the destructor of the
    // derived class has been called.
  }

private:
  Envoy::Thread::ThreadPtr thread_;
  Envoy::Thread::ThreadFactory* thread_factory_;
  absl::Notification thread_is_set_; // Whether |thread_| is set in parent.
};

} // namespace quic
