#include "source/common/common/thread.h"

#include <thread>

#include "source/common/common/assert.h"
#include "source/common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Thread {

struct ThreadIds {
  using Singleton = InjectableSingleton<ThreadIds>;

  bool inMainOrTestThread() const {
    std::thread::id id = std::this_thread::get_id();
    absl::MutexLock lock(&mutex_);
    return ((main_thread_id_.has_value() && (main_thread_id_.value() == id)) ||
            (test_thread_id_.has_value() && (test_thread_id_.value() == id)));
  }

  static ThreadIds& get() {
    ThreadIds* singleton = Singleton::getExisting();
    ASSERT(singleton != nullptr, "Thread::MainThread or Thread::TestTHread must be instantiated");
    return *singleton;
  }

  static ThreadIds& acquire() {
    ThreadIds* singleton = Singleton::getExisting();
    if (singleton == nullptr) {
      singleton = new ThreadIds;
      Singleton::initialize(singleton);
    } /* else {
       absl::MutexLock lock(&singleton->mutex_);
       ++singleton->ref_count_;
       }*/
    return *singleton;
  }

  void release() EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    /*if (--ref_count_ == 0) {
      Singleton::clear();
      delete this;
      }*/
  }

  void releaseMainThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(std::this_thread::get_id() == main_thread_id_.value());
    if (--main_thread_use_count_ == 0) {
      main_thread_id_ = absl::nullopt;
    }
    release();
  }

  void releaseTestThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(std::this_thread::get_id() == test_thread_id_.value());
    if (--test_thread_use_count_ == 0) {
      test_thread_id_ = absl::nullopt;
    }
    release();
  }

  void registerMainThread() {
    absl::MutexLock lock(&mutex_);
    ++main_thread_use_count_;
    if (main_thread_id_.has_value()) {
      ASSERT(std::this_thread::get_id() == main_thread_id_.value());
    } else {
      main_thread_id_ = std::this_thread::get_id();
    }
  }

  void registerTestThread() {
    absl::MutexLock lock(&mutex_);
    ++test_thread_use_count_;
    if (test_thread_id_.has_value()) {
      ASSERT(std::this_thread::get_id() == test_thread_id_.value());
    } else {
      test_thread_id_ = std::this_thread::get_id();
    }
  }

private:
  absl::optional<std::thread::id> main_thread_id_ GUARDED_BY(mutex_);
  absl::optional<std::thread::id> test_thread_id_ GUARDED_BY(mutex_);
  int32_t main_thread_use_count_ GUARDED_BY(mutex_) = 0;
  int32_t test_thread_use_count_ GUARDED_BY(mutex_) = 0;
  // int32_t ref_count_ GUARDED_BY(mutex_) = 1;
  mutable absl::Mutex mutex_;
};

bool MainThread::isMainThread() { return ThreadIds::get().inMainOrTestThread(); }

TestThread::TestThread() { ThreadIds::acquire().registerTestThread(); }

TestThread::~TestThread() { ThreadIds::get().releaseTestThread(); }

MainThread::MainThread() { ThreadIds::acquire().registerMainThread(); }

MainThread::~MainThread() { ThreadIds::get().releaseMainThread(); }

} // namespace Thread
} // namespace Envoy
