#pragma once

#include <windows.h>

// <windows.h> defines some macros that interfere with our code, so undef them
#undef DELETE
#undef GetMessage

#include <functional>

#include "absl/hash/hash.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

class ThreadIdImplWin32 : public ThreadId {
public:
  ThreadIdImplWin32(DWORD id);

  // Thread::ThreadId
  std::string debugString() const override;
  bool isCurrentThreadId() const override;
  bool operator==(const ThreadId& b) const override {
    const ThreadIdImplPosix* casted_b = dynamic_cast<const ThreadIdImplPosix*>(&b);
    return (casted_b != nullptr) && (id_ == casted_b->id_);
  }

private:
  void HashValue(absl::HashState state) const override {
    absl::HashState::combine(std::move(state), id_);
  }
  DWORD id_;
};

/**
 * Wrapper for a win32 thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplWin32 : public Thread {
public:
  ThreadImplWin32(std::function<void()> thread_routine);
  ~ThreadImplWin32();

  // Thread::Thread
  void join() override;

private:
  std::function<void()> thread_routine_;
  HANDLE thread_handle_;
};

/**
 * Implementation of ThreadFactory
 */
class ThreadFactoryImplWin32 : public ThreadFactory {
public:
  // Thread::ThreadFactory
  ThreadPtr createThread(std::function<void()> thread_routine) override;
  ThreadIdPtr currentThreadId() override;
};

} // namespace Thread
} // namespace Envoy
