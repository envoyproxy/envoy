#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

namespace Envoy {
namespace Thread {

class Thread {
public:
  virtual ~Thread() {}

  /**
   * Join on thread exit.
   */
  virtual void join() PURE;
};

typedef std::unique_ptr<Thread> ThreadPtr;

/**
 * Interface providing a mechanism for creating threads.
 */
class ThreadFactory {
public:
  virtual ~ThreadFactory() {}

  /**
   * Create a thread.
   * @param thread_routine supplies the function to invoke in the thread.
   */
  virtual ThreadPtr createThread(std::function<void()> thread_routine) PURE;
};

/**
 * Like the C++11 "basic lockable concept" but a pure virtual interface vs. a template, and
 * with thread annotations.
 */
class LOCKABLE BasicLockable {
public:
  virtual ~BasicLockable() {}

  virtual void lock() EXCLUSIVE_LOCK_FUNCTION() PURE;
  virtual bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) PURE;
  virtual void unlock() UNLOCK_FUNCTION() PURE;
};

} // namespace Thread
} // namespace Envoy
