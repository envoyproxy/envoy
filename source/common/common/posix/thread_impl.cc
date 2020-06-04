#include "common/common/assert.h"
#include "common/common/thread_impl.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"

#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace Envoy {
namespace Thread {

namespace {

int64_t getCurrentThreadId() {
#ifdef __linux__
  return static_cast<int64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return tid;
#else
#error "Enable and test pthread id retrieval code for you arch in pthread/thread_impl.cc"
#endif
}

} // namespace

// See https://www.man7.org/linux/man-pages/man3/pthread_setname_np.3.html.
// The maximum thread name is 16 bytes including the terminating nul byte,
// so we need to truncate the string_view to 15 bytes.
#define PTHREAD_MAX_LEN_INCLUDING_NULL_BYTE 16

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplPosix : public Thread {
public:
  ThreadImplPosix(std::function<void()> thread_routine, OptionsOptConstRef options)
      : thread_routine_(std::move(thread_routine)) {
    if (options) {
      name_ = (std::string(options->name_.substr(0, PTHREAD_MAX_LEN_INCLUDING_NULL_BYTE - 1)));
    }
    RELEASE_ASSERT(Logger::Registry::initialized(), "");
    const int rc = pthread_create(
        &thread_handle_, nullptr,
        [](void* arg) -> void* {
          auto* thread = static_cast<ThreadImplPosix*>(arg);

          // Block at thread start waiting for setup to be complete in the initiating thread.
          // For example, we want to set the debug name of the thread.
          thread->start_.WaitForNotification();

          thread->thread_routine_();
          return nullptr;
        },
        this);
    RELEASE_ASSERT(rc == 0, "");
    if (!name_.empty()) {
      const int set_name_rc = pthread_setname_np(thread_handle_, name_.c_str());
      RELEASE_ASSERT(set_name_rc == 0, absl::StrCat("Error ", set_name_rc, " setting name '", name_,
                                                    "': ", strerror(set_name_rc)));
#ifndef NDEBUG
      // Verify that the name got written into the thread as expected.
      char buf[PTHREAD_MAX_LEN_INCLUDING_NULL_BYTE];
      const int get_name_rc = pthread_getname_np(thread_handle_, buf, sizeof(buf));
      RELEASE_ASSERT(get_name_rc == 0, absl::StrCat("Error ", get_name_rc, " setting name '", name_,
                                                    "': ", strerror(get_name_rc)));
#endif
    }
    start_.Notify();
  }

  std::string name() const override { return name_; }

  // Thread::Thread
  void join() override;

private:
  std::function<void()> thread_routine_;
  pthread_t thread_handle_;
  std::string name_;
  absl::Notification start_;
};

void ThreadImplPosix::join() {
  const int rc = pthread_join(thread_handle_, nullptr);
  RELEASE_ASSERT(rc == 0, "");
}

ThreadPtr ThreadFactoryImplPosix::createThread(std::function<void()> thread_routine,
                                               OptionsOptConstRef options) {
  return std::make_unique<ThreadImplPosix>(thread_routine, options);
}

ThreadId ThreadFactoryImplPosix::currentThreadId() { return ThreadId(getCurrentThreadId()); }

} // namespace Thread
} // namespace Envoy
