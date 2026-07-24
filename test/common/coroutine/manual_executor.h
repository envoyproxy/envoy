#pragma once

#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>

#include "source/common/common/assert.h"
#include "source/common/coroutine/executor.h"

namespace Envoy {
namespace Coroutine {

/**
 * Deterministic test `Executor`. `schedule()` queues handles instead of running
 * them; `drain()` resumes queued handles in FIFO order. This lets a test step a
 * coroutine forward one scheduling boundary at a time.
 *
 * It has no timer backend (timers require the libevent-backed dispatcher), so
 * `createTimer()` is unsupported. Tests that need timers (TimerAwaitable) use
 * `DispatcherExecutor` with a real or simulated-time dispatcher; core Task and
 * cancellation tests drive leaves manually and never create a timer.
 */
class ManualExecutor : public Executor {
public:
  void schedule(std::coroutine_handle<> handle) override { queue_.push_back(handle); }

  Event::TimerPtr createTimer(std::function<void()>) override {
    PANIC("ManualExecutor does not support timers; use DispatcherExecutor");
  }

  // Resume all queued handles (FIFO). Handles scheduled during a resume are
  // picked up in the same drain.
  void drain() {
    while (!queue_.empty()) {
      std::coroutine_handle<> handle = queue_.front();
      queue_.pop_front();
      handle.resume();
    }
  }

  bool empty() const { return queue_.empty(); }
  size_t size() const { return queue_.size(); }

private:
  std::deque<std::coroutine_handle<>> queue_;
};

} // namespace Coroutine
} // namespace Envoy
