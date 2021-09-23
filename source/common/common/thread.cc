#include "source/common/common/thread.h"

#include <thread>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

namespace Envoy {
namespace Thread {

namespace {

struct ThreadIds {
  bool inMainOrTestThread() const {
    std::thread::id id = std::this_thread::get_id();
    absl::MutexLock lock(&mutex_);
    return ((main_thread_id_.has_value() && (main_thread_id_.value() == id)) ||
            (test_thread_id_.has_value() && (test_thread_id_.value() == id)));
  }

  static ThreadIds& get() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ThreadIds); }

  void releaseMainThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(std::this_thread::get_id() == main_thread_id_.value());
    if (--main_thread_use_count_ == 0) {
      main_thread_id_ = absl::nullopt;
    }
  }

  void releaseTestThread() {
    absl::MutexLock lock(&mutex_);
    ASSERT(std::this_thread::get_id() == test_thread_id_.value());
    if (--test_thread_use_count_ == 0) {
      test_thread_id_ = absl::nullopt;
    }
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
