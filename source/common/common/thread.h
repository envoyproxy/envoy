#pragma once

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>

#include "envoy/thread/thread.h"

#include "source/common/common/assert.h"
#include "source/common/common/non_copyable.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Thread {

/**
 * Implementation of BasicLockable
 */
class MutexBasicLockable : public BasicLockable {
public:
  // BasicLockable
  void lock() ABSL_EXCLUSIVE_LOCK_FUNCTION() override { mutex_.Lock(); }
  bool tryLock() ABSL_EXCLUSIVE_TRYLOCK_FUNCTION(true) override { return mutex_.TryLock(); }
  void unlock() ABSL_UNLOCK_FUNCTION() override { mutex_.Unlock(); }

private:
  friend class CondVar;
  absl::Mutex mutex_;
};

/**
 * Implementation of condvar, based on MutexLockable. This interface is a hybrid
 * between std::condition_variable and absl::CondVar.
 */
class CondVar {
public:
  enum class WaitStatus {
    Timeout,
    NoTimeout, // Success or Spurious
  };

  /**
   * Note that it is not necessary to be holding an associated mutex to call
   * notifyOne or notifyAll. See the discussion in
   *     http://en.cppreference.com/w/cpp/thread/condition_variable_any/notify_one
   * for more details.
   */
  void notifyOne() noexcept { condvar_.Signal(); }
  void notifyAll() noexcept { condvar_.SignalAll(); };

  /**
   * wait() and waitFor do not throw, and never will, as they are based on
   * absl::CondVar, so it's safe to pass the a mutex to wait() directly, even if
   * it's also managed by a LockGuard. See definition of CondVar in
   * source/source/thread.h for an alternate implementation, which does not work
   * with thread annotation.
   */
  void wait(MutexBasicLockable& mutex) noexcept ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    condvar_.Wait(&mutex.mutex_);
  }

  /**
   * @return WaitStatus whether the condition timed out or not.
   */
  template <class Rep, class Period>
  WaitStatus waitFor(MutexBasicLockable& mutex,
                     std::chrono::duration<Rep, Period> duration) noexcept
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    return condvar_.WaitWithTimeout(&mutex.mutex_, absl::FromChrono(duration))
               ? WaitStatus::Timeout
               : WaitStatus::NoTimeout;
  }

private:
  // Note: alternate implementation of this class based on std::condition_variable_any
  // https://gist.github.com/jmarantz/d22b836cee3ca203cc368553eda81ce5
  // does not currently work well with thread-annotation.
  absl::CondVar condvar_;
};

enum class AtomicPtrAllocMode { DoNotDelete, DeleteOnDestruct };

// Manages an array of atomic pointers to T, providing a relatively
// contention-free mechanism to lazily get a T* at an index, where the caller
// provides a mechanism to instantiate a T* under lock, if one has not already
// been stored at that index.
//
// alloc_mode controls whether allocated T* entries should be deleted on
// destruction of the array. This should be set to AtomicPtrAllocMode::DoNotDelete
// if the T* returned from MakeObject are managed by the caller.
template <class T, uint32_t size, AtomicPtrAllocMode alloc_mode>
class AtomicPtrArray : NonCopyable {
public:
  AtomicPtrArray() {
    for (std::atomic<T*>& atomic_ref : data_) {
      atomic_ref = nullptr;
    }
  }

  ~AtomicPtrArray() {
    if (alloc_mode == AtomicPtrAllocMode::DeleteOnDestruct) {
      for (std::atomic<T*>& atomic_ref : data_) {
        T* ptr = atomic_ref.load();
        if (ptr != nullptr) {
          delete ptr;
        }
      }
    }
  }

  // User-defined function for allocating an object. This will be called
  // under a lock controlled by this class, so MakeObject will not race
  // against itself. MakeObject is allowed to return nullptr, in which
  // case the next call to get() will call MakeObject again.
  using MakeObject = std::function<T*()>;

  /*
   * Returns an already existing T* at index, or calls make_object to
   * instantiate and save the T* under lock.
   *
   * @param index the Index to look up.
   * @param make_object function to call under lock to make a T*.
   * @return The new or already-existing T*, possibly nullptr if make_object returns nullptr.
   */
  T* get(uint32_t index, const MakeObject& make_object) {
    std::atomic<T*>& atomic_ref = data_[index];

    // First, use an atomic load to see if the object has already been allocated.
    if (atomic_ref.load() == nullptr) {
      absl::MutexLock lock(&mutex_);

      // If that fails, check again under lock as two threads might have raced
      // to create the object.
      if (atomic_ref.load() == nullptr) {
        atomic_ref = make_object();
      }
    }
    return atomic_ref.load();
  }

  /**
   * Returns whether the underlying pointer at the index is null.
   *
   * @param index the Index to look up.
   * @return true if the underlying pointer at the index is null.
   */
  bool isNull(uint32_t index) const { return data_[index].load() == nullptr; }

private:
  std::atomic<T*> data_[size];
  absl::Mutex mutex_;
};

// Manages a pointer to T, providing a relatively contention-free mechanism to
// lazily create a T*, where the caller provides a mechanism to instantiate a
// T* under lock, if one has not already been stored.
//
// alloc_mode controls whether allocated T* objects should be deleted on
// destruction of the AtomicObject. This should be set to
// AtomicPtrAllocMode::DoNotDelete if the T* returned from MakeObject are managed
// by the caller.
template <class T, AtomicPtrAllocMode alloc_mode>
class AtomicPtr : private AtomicPtrArray<T, 1, alloc_mode> {
public:
  using BaseClass = AtomicPtrArray<T, 1, alloc_mode>;
  using typename BaseClass::MakeObject;

  /*
   * Returns an already existing T*, or calls make_object to instantiate and
   * save the T* under lock.
   *
   * @param make_object function to call under lock to make a T*.
   * @return The new or already-existing T*, possibly nullptr if make_object returns nullptr.
   */
  T* get(const MakeObject& make_object) { return BaseClass::get(0, make_object); }

  /**
   * @return true if the underlying pointer is null.
   */
  bool isNull() const { return BaseClass::isNull(0); }
};

// We use platform-specific functions to determine whether the current thread is
// the "test thread". It is only valid to call isTestThread() on platforms where
// these functions are available. Currently this is available only on apple and
// linux.
#if defined(__linux__) || defined(__APPLE__)
#define TEST_THREAD_SUPPORTED 1
#else
#define TEST_THREAD_SUPPORTED 0
#endif

// Context for determining whether we are in the test thread.
class TestThread {
public:
#if TEST_THREAD_SUPPORTED
  /**
   * @return whether the current thread is the test thread.
   *
   * Use of the macros ASSERT_IS_TEST_THREAD() and ASSERT_IS_NOT_TEST_THREAD()
   * are preferred to avoid issues on platforms where detecting the test-thread
   * is not supported.
   */
  static bool isTestThread();
#endif
};

// RAII object to declare the MainThread. This should be declared in the thread
// function or equivalent.
//
// Generally we expect MainThread to be instantiated only once or twice. It has
// to be instantiated prior to OptionsImpl being created, so it needs to be in
// instantiated from main_common(). In addition, it is instantiated by
// ThreadLocal implementation to get the correct behavior for tests that do not
// instantiate main.
//
// In general, nested instantiations are allowed as long as the thread ID does
// not change.
class MainThread {
public:
  MainThread();
  ~MainThread();

#if TEST_THREAD_SUPPORTED
  /**
   * @return whether the current thread is the main thread or test thread.
   *
   * Determines whether we are currently running on the main-thread or
   * test-thread. We need to allow for either one because we don't establish
   * the full threading model in all unit tests.
   *
   * Use of the macros ASSERT_IS_TEST_THREAD() and ASSERT_IS_NOT_TEST_THREAD()
   * are preferred to avoid issues on platforms where detecting the test-thread
   * is not supported.
   */
  static bool isMainOrTestThread() { return isMainThread() || TestThread::isTestThread(); }
#endif

  /**
   * @return whether the current thread is the main thread.
   */
  static bool isMainThread();

  /**
   * @return whether a MainThread has been instantiated.
   */
  static bool isMainThreadActive();
};

#define END_TRY }

#ifdef ENVOY_DISABLE_EXCEPTIONS
#define TRY_NEEDS_AUDIT {
#else
// TODO(chaoqinli-1123): Remove this macros after we have removed all the exceptions from data
// plane.
#define TRY_NEEDS_AUDIT try {
#endif

#ifdef ENVOY_DISABLE_EXCEPTIONS
#define CATCH(ExceptionType, Handler)
#else
#define CATCH(Exception, Handler)                                                                  \
  catch (Exception) {                                                                              \
    Handler                                                                                        \
  }
#endif

#ifdef ENVOY_DISABLE_EXCEPTIONS
#define MULTI_CATCH(ExceptionType, Handler, Handler2)
#else
#define MULTI_CATCH(Exception, Handler, Handler2)                                                  \
  catch (Exception) {                                                                              \
    Handler                                                                                        \
  }                                                                                                \
  catch (...) {                                                                                    \
    Handler2                                                                                       \
  }
#endif

// These convenience macros assert properties of the threading system, when
// feasible. There is a platform-specific mechanism for determining whether the
// current thread is from main(), which we call the "test thread", and if that
// method is not available on the current platform we must skip the assertions.
//
// Note that the macros are all no-ops if there are any SkipAsserts instances
// allocated.
#ifdef NDEBUG

#define ASSERT_IS_TEST_THREAD()
#define ASSERT_IS_MAIN_OR_TEST_THREAD()
#define ASSERT_IS_NOT_TEST_THREAD()
#define ASSERT_IS_NOT_MAIN_OR_TEST_THREAD()

#elif TEST_THREAD_SUPPORTED

#define ASSERT_IS_TEST_THREAD()                                                                    \
  ASSERT(Thread::SkipAsserts::skip() || Thread::TestThread::isTestThread())
#define ASSERT_IS_MAIN_OR_TEST_THREAD()                                                            \
  ASSERT(Thread::SkipAsserts::skip() || Thread::TestThread::isTestThread() ||                      \
         Thread::MainThread::isMainThread())
#define ASSERT_IS_NOT_TEST_THREAD()                                                                \
  ASSERT(Thread::SkipAsserts::skip() || !Thread::TestThread::isTestThread())
#define ASSERT_IS_NOT_MAIN_OR_TEST_THREAD()                                                        \
  ASSERT(Thread::SkipAsserts::skip() ||                                                            \
         (!Thread::MainThread::isMainThread() && !Thread::TestThread::isTestThread()))

#else // !TEST_THREAD_SUPPORTED -- test-thread checks are skipped

#define ASSERT_IS_TEST_THREAD()
#define ASSERT_IS_MAIN_OR_TEST_THREAD()
#define ASSERT_IS_NOT_TEST_THREAD()
#define ASSERT_IS_NOT_MAIN_OR_TEST_THREAD() ASSERT(!Thread::MainThread::isMainThread())

#endif

#ifdef ENVOY_DISABLE_EXCEPTIONS
#define TRY_ASSERT_MAIN_THREAD {
#else
/**
 * To improve exception safety in data plane, we plan to forbid the use of raw
 * try in the core code base. This macros uses main thread assertion to make
 * sure that exceptions aren't thrown from worker thread.
 */
#define TRY_ASSERT_MAIN_THREAD                                                                     \
  try {                                                                                            \
    ASSERT_IS_MAIN_OR_TEST_THREAD();
#endif

/**
 * RAII class to override thread assertions checks in the macros:
 *
 *   TRY_ASSERT_MAIN_THREAD
 *   ASSERT_IS_TEST_THREAD()
 *   ASSERT_IS_MAIN_OR_TEST_THREAD()
 *   ASSERT_IS_NOT_TEST_THREAD()
 *   ASSERT_IS_NOT_MAIN_OR_TEST_THREAD()
 *
 * Those macros will be no-ops while there is a SkipAsserts object
 * alive. SkipAsserts declarations can be nested.
 *
 * The state of the assertion-skipping can also be checked by calling static
 * method SkipAsserts::skip().
 *
 * This class is intended to be instantiated on the stack in a limited scope.
 */
class SkipAsserts {
public:
  SkipAsserts();
  ~SkipAsserts();

  /**
   * @return whether thread-related assertions should be skipped.
   */
  static bool skip();
};

} // namespace Thread
} // namespace Envoy
