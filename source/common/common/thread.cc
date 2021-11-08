#include "source/common/common/thread.h"

#include <thread>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Thread {

namespace {

// Singleton structure capturing which thread is the main dispatcher thread, and
// which is the test thread. This info is used for assertions around catching
// exceptions and accessing data structures which are not mutex-protected, and
// are expected only from the main thread.
//
// TODO(jmarantz): avoid the singleton and instead have this object owned
// by the ThreadFactory. That will require plumbing the API::API into all
// call-sites for isMainThread(), which might be a bit of work, but will make
// tests more hermetic.
struct ThreadIds {
  bool inMainThread() const {
    // We don't take the lock when testing the thread IDs, as they are atomic,
    // and are cleared when being released. All possible thread orderings
    // result in the correct result even without a lock.
    return std::this_thread::get_id() == main_thread_id_;
  }

  bool isMainThreadActive() const {
    absl::MutexLock lock(&mutex_);
    return main_thread_use_count_ != 0;
  }

  // Returns a singleton instance of this. The instance is never freed.
  static ThreadIds& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ThreadIds); }

  // Call this when the MainThread exits. Nested semantics are supported, so
  // that if multiple MainThread instances are declared, we unwind them
  // properly.
  void releaseMainThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(main_thread_use_count_ > 0);
    ASSERT(std::this_thread::get_id() == main_thread_id_);
    if (--main_thread_use_count_ == 0) {
      // Clearing the thread ID when its use-count goes to zero allows us
      // to read the atomic without taking a lock.
      main_thread_id_ = std::thread::id{};
    }
  }

  // Declares current thread as the main one, or verifies that the current
  // thread matches any previous declarations.
  void registerMainThread() {
    absl::MutexLock lock(&mutex_);
    if (++main_thread_use_count_ > 1) {
      ASSERT(std::this_thread::get_id() == main_thread_id_);
    } else {
      main_thread_id_ = std::this_thread::get_id();
    }
  }

  // Methods to track how many SkipAssert objects are instantiated.
  void incSkipAsserts() { ++skip_asserts_; }
  void decSkipAsserts() { --skip_asserts_; }
  bool skipAsserts() const { return skip_asserts_ > 0; }

private:
  // The atomic thread IDs can be read without a mutex, but they are written
  // under a mutex so that they are consistent with their use_counts. this
  // avoids the possibility of two threads racing to claim being the main/test
  // thread.
  std::atomic<std::thread::id> main_thread_id_;

  int32_t main_thread_use_count_ GUARDED_BY(mutex_) = 0;
  mutable absl::Mutex mutex_;

  std::atomic<uint32_t> skip_asserts_{};
};

} // namespace

bool MainThread::isMainThread() { return ThreadIds::get().inMainThread(); }

bool MainThread::isMainThreadActive() { return ThreadIds::get().isMainThreadActive(); }

MainThread::MainThread() { ThreadIds::get().registerMainThread(); }

MainThread::~MainThread() { ThreadIds::get().releaseMainThread(); }

#if TEST_THREAD_SUPPORTED
bool TestThread::isTestThread() {
  // Keep this implementation consistent with TEST_THREAD_SUPPORTED, defined in thread.h.
  // https://stackoverflow.com/questions/4867839/how-can-i-tell-if-pthread-self-is-the-main-first-thread-in-the-process
#ifdef __linux__
  return getpid() == syscall(SYS_gettid);
#elif defined(__APPLE__)
  return pthread_main_np() != 0;
#endif
  // Note: final #else fallback omitted intentionally.
}
#endif

SkipAsserts::SkipAsserts() { ThreadIds::get().incSkipAsserts(); }

SkipAsserts::~SkipAsserts() { ThreadIds::get().decSkipAsserts(); }

bool SkipAsserts::skip() { return ThreadIds::get().skipAsserts(); }

} // namespace Thread
} // namespace Envoy
