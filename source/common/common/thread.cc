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
  // Determines whether we are currently running on the main-thread or
  // test-thread. We need to allow for either one because we don't establish
  // the full threading model in all unit tests.
  bool inMainOrTestThread() const {
    // We don't take the lock when testing the thread IDs, as they are atomic,
    // and are cleared when being released. All possible thread orderings
    // result in the correct result even without a lock.
    std::thread::id id = std::this_thread::get_id();
    return main_thread_id_ == id || test_thread_id_ == id;
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

  // Call this when the TestThread exits. Nested semantics are supported, so
  // that if multiple TestThread instances are declared, we unwind them
  // properly.
  void releaseTestThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(test_thread_use_count_ > 0);
    ASSERT(std::this_thread::get_id() == test_thread_id_);
    if (--test_thread_use_count_ == 0) {
      // Clearing the thread ID when its use-count goes to zero allows us
      // to read the atomic without taking a lock.
      test_thread_id_ = std::thread::id{};
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

  // Declares current thread as the test thread, or verifies that the current
  // thread matches any previous declarations.
  void registerTestThread() {
    absl::MutexLock lock(&mutex_);
    if (++test_thread_use_count_ > 1) {
      ASSERT(std::this_thread::get_id() == test_thread_id_);
    } else {
      test_thread_id_ = std::this_thread::get_id();
    }
  }

private:
  // The atomic thread IDs can be read without a mutex, but they are written
  // under a mutex so that they are consistent with their use_counts. this
  // avoids the possibility of two threads racing to claim being the main/test
  // thread.
  std::atomic<std::thread::id> main_thread_id_;
  std::atomic<std::thread::id> test_thread_id_;

  int32_t main_thread_use_count_ GUARDED_BY(mutex_) = 0;
  int32_t test_thread_use_count_ GUARDED_BY(mutex_) = 0;
  mutable absl::Mutex mutex_;
};

} // namespace

bool MainThread::isMainThread() { return ThreadIds::get().inMainOrTestThread(); }

TestThread::TestThread() { ThreadIds::get().registerTestThread(); }

TestThread::~TestThread() { ThreadIds::get().releaseTestThread(); }

MainThread::MainThread() { ThreadIds::get().registerMainThread(); }

MainThread::~MainThread() { ThreadIds::get().releaseMainThread(); }

} // namespace Thread
} // namespace Envoy
