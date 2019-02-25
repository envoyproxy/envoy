#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

namespace Envoy {
namespace Thread {

class ThreadId {
public:
  virtual ~ThreadId() {}

  virtual std::string debugString() const PURE;
  virtual bool isCurrentThreadId() const PURE;
};

typedef std::unique_ptr<ThreadId> ThreadIdPtr;

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

  /**
   * Return the current system thread ID
   */
  virtual ThreadIdPtr currentThreadId() PURE;
};

/**
 * A static singleton to the ThreadFactory corresponding to the build platform.
 *
 * The singleton must be initialized via set() early in main() with the appropriate ThreadFactory
 * (see source/exe/{posix,win32}/platform_impl.h).
 *
 * This static singleton is an exception to Envoy's established practice for handling of singletons,
 * which are typically registered with and accessed via the Envoy::Singleton::Manager. Reasons for
 * the exception include drastic simplification of thread safety assertions; e.g.:
 *   ASSERT(ThreadFactorySingleton::get()->currentThreadId() == original_thread_id_);
 */
class ThreadFactorySingleton {
public:
  /**
   * Returns a reference to the platform dependent ThreadFactory.
   */
  static ThreadFactory& get() { return *thread_factory_; }

  /**
   * Sets the singleton to the supplied thread_factory.
   * @param thread_factory the ThreadFactory instance to be pointed to by this singleton.
   */
  static void set(ThreadFactory* thread_factory);

private:
  static ThreadFactory* thread_factory_;
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
