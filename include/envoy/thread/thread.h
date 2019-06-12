#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

namespace Envoy {
namespace Thread {

class ThreadId {
public:
  virtual ~ThreadId() = default;

  virtual std::string debugString() const PURE;
  virtual bool isCurrentThreadId() const PURE;
};

using ThreadIdPtr = std::unique_ptr<ThreadId>;

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
  virtual ThreadIdPtr currentThreadId() PURE;
};

/**
 * Like the C++11 "basic lockable concept" but a pure virtual interface vs. a template, and
 * with thread annotations.
 */
class LOCKABLE BasicLockable {
public:
  virtual ~BasicLockable() = default;

  virtual void lock() EXCLUSIVE_LOCK_FUNCTION() PURE;
  virtual bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) PURE;
  virtual void unlock() UNLOCK_FUNCTION() PURE;
};

} // namespace Thread
} // namespace Envoy
