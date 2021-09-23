#include "source/common/common/thread.h"

#include <thread>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Thread {

struct ThreadIds {
  bool inMainThread(std::thread::id id) const {
    return main_thread_id_.has_value() && (main_thread_id_.value() == id);
  }

  bool inTestThread(std::thread::id id) const {
    return test_thread_id_.has_value() && (test_thread_id_.value() == id);
  }

  static ThreadIds* get() {
    ASSERT(singleton_ != nullptr, "Thread::MainThread or Thread::TestTHread must be instantiated");
    return singleton_;
  }

  static ThreadIds* acquire() {
    if (singleton_ == nullptr) {
      singleton_ = new ThreadIds;
    } else {
      ++singleton_->ref_count_;
    }
    return singleton_;
  }

  void release() {
    if (--ref_count_ == 0) {
      delete singleton_;
      singleton_ = nullptr;
    }
  }

  void releaseMainThread() {
    ASSERT(std::this_thread::get_id() == main_thread_id_.value());
    if (--main_thread_use_count_ == 0) {
      main_thread_id_ = absl::nullopt;
    }
    release();
  }

  void releaseTestThread() {
    ASSERT(std::this_thread::get_id() == test_thread_id_.value());
    if (--test_thread_use_count_ == 0) {
      test_thread_id_ = absl::nullopt;
    }
    release();
  }

  void registerMainThread() {
    ++main_thread_use_count_;
    if (main_thread_id_.has_value()) {
      ASSERT(std::this_thread::get_id() == main_thread_id_.value());
    } else {
      main_thread_id_ = std::this_thread::get_id();
    }
  }

  void registerTestThread() {
    ++test_thread_use_count_;
    if (test_thread_id_.has_value()) {
      ASSERT(std::this_thread::get_id() == test_thread_id_.value());
    } else {
      test_thread_id_ = std::this_thread::get_id();
    }
  }

private:
  absl::optional<std::thread::id> main_thread_id_;
  absl::optional<std::thread::id> test_thread_id_;
  std::atomic<int32_t> ref_count_{1};
  std::atomic<int32_t> main_thread_use_count_{0};
  std::atomic<int32_t> test_thread_use_count_{0};
  static ThreadIds* singleton_;
};

ThreadIds* ThreadIds::singleton_ = nullptr;

bool MainThread::isMainThread() {
  ThreadIds* ids = ThreadIds::get();
  std::thread::id id = std::this_thread::get_id();
  return ids->inMainThread(id) || ids->inTestThread(id);
}

TestThread::TestThread() { ThreadIds::acquire()->registerTestThread(); }

TestThread::~TestThread() { ThreadIds::get()->releaseTestThread(); }

MainThread::MainThread() { ThreadIds::acquire()->registerMainThread(); }

MainThread::~MainThread() { ThreadIds::get()->releaseMainThread(); }

} // namespace Thread
} // namespace Envoy
