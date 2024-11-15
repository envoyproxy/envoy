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
// are expected to be used for debug mode checks.
//
// TODO(jmarantz): avoid the singleton and instead have this object owned
// by the ThreadFactory. That will require plumbing the API::API into all
// call-sites for isMainThread(), which might be a bit of work, but will make
// tests more hermetic.
struct ThreadIds {
  bool inMainThread() const {
    absl::MutexLock lock(&mutex_);
    return main_threads_to_usage_count_.find(std::this_thread::get_id()) !=
           main_threads_to_usage_count_.end();
  }

  bool isMainThreadActive() const {
    absl::MutexLock lock(&mutex_);
    return !main_threads_to_usage_count_.empty();
  }

  // Returns a singleton instance of this. The instance is never freed.
  static ThreadIds& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ThreadIds); }

  // Call this from the context of MainThread when it exits.
  void releaseMainThread() {
    absl::MutexLock lock(&mutex_);
    auto it = main_threads_to_usage_count_.find(std::this_thread::get_id());
    if (!skipAsserts()) {
      ASSERT(it != main_threads_to_usage_count_.end());
    }
    if (it != main_threads_to_usage_count_.end()) {
      it->second--;
      if (it->second == 0) {
        main_threads_to_usage_count_.erase(it);
      }
    }
  }

  // Declares current thread as the main one, or verifies that the current
  // thread matches any previous declarations.
  void registerMainThread() {
    absl::MutexLock lock(&mutex_);
    auto it = main_threads_to_usage_count_.find(std::this_thread::get_id());
    if (it == main_threads_to_usage_count_.end()) {
      it = main_threads_to_usage_count_.insert({std::this_thread::get_id(), 0}).first;
    }
    it->second++;
  }

  // Methods to track how many SkipAssert objects are instantiated.
  void incSkipAsserts() { ++skip_asserts_; }
  void decSkipAsserts() { --skip_asserts_; }
  bool skipAsserts() const { return skip_asserts_ > 0; }

private:
  mutable absl::Mutex mutex_;
  std::atomic<uint32_t> skip_asserts_{};
  absl::flat_hash_map<std::thread::id, uint32_t>
      main_threads_to_usage_count_ ABSL_GUARDED_BY(mutex_);
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
