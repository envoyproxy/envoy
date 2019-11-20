#pragma once

#include <functional>
#include <limits>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

namespace Envoy {
namespace Thread {

/**
 * An id for a thread.
 */
class ThreadId {
public:
  ThreadId() : id_(std::numeric_limits<int64_t>::min()) {}
  explicit ThreadId(int64_t id) : id_(id) {}

  std::string debugString() const { return std::to_string(id_); }
  bool isEmpty() const { return *this == ThreadId(); }
  friend bool operator==(ThreadId lhs, ThreadId rhs) { return lhs.id_ == rhs.id_; }
  friend bool operator!=(ThreadId lhs, ThreadId rhs) { return lhs.id_ != rhs.id_; }
  template <typename H> friend H AbslHashValue(H h, ThreadId id) {
    return H::combine(std::move(h), id.id_);
  }

private:
  int64_t id_;
};

class Thread {
public:
  virtual ~Thread() = default;

  /**
   * Join on thread exit.
   */
  virtual void join() PURE;
};

using ThreadPtr = std::unique_ptr<Thread>;

/**
 * Interface providing a mechanism for creating threads.
 */
class ThreadFactory {
public:
  virtual ~ThreadFactory() = default;

  /**
   * Create a thread.
   * @param thread_routine supplies the function to invoke in the thread.
   */
  virtual ThreadPtr createThread(std::function<void()> thread_routine) PURE;

  /**
   * Return the current system thread ID
   */
  virtual ThreadId currentThreadId() PURE;
};

/**
 * Like the C++11 "basic lockable concept" but a pure virtual interface vs. a template, and
 * with thread annotations.
 */
class ABSL_LOCKABLE BasicLockable {
public:
  virtual ~BasicLockable() = default;

  virtual void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() PURE;
  virtual bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) PURE;
  virtual void unlock() ABSL_UNLOCK_FUNCTION() PURE;
};

} // namespace Thread
} // namespace Envoy
