#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"

#include "common/common/thread_annotations.h"

#ifdef __linux__
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(WIN32)
#include <Windows.h>
// <windows.h> defines some macros that interfere with our code, so undef them
#undef DELETE
#undef GetMessage
#endif

namespace Envoy {
namespace Thread {

#if !defined(WIN32)
typedef int32_t ThreadId;
typedef pthread_t ThreadHandle;
#else
typedef DWORD ThreadId;
typedef HANDLE ThreadHandle;
#endif

class Thread {
public:
  virtual ~Thread() {}

  /**
   * Join on thread exit.
   */
  virtual void join() PURE;

  virtual ThreadHandle handle() const PURE;
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
