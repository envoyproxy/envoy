#include "common/common/assert.h"
#include "common/common/thread_impl.h"

#include "absl/strings/str_cat.h"

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
#define PTHREAD_MAX_THREADNAME_LEN_INCLUDING_NULL_BYTE 16

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
class ThreadImplPosix : public Thread {
public:
  ThreadImplPosix(std::function<void()> thread_routine, OptionsOptConstRef options)
      : thread_routine_(std::move(thread_routine)) {
    if (options) {
      name_ = options->name_.substr(0, PTHREAD_MAX_THREADNAME_LEN_INCLUDING_NULL_BYTE - 1);
    }
    RELEASE_ASSERT(Logger::Registry::initialized(), "");
    const int rc = pthread_create(
        &thread_handle_, nullptr,
        [](void* arg) -> void* {
          static_cast<ThreadImplPosix*>(arg)->thread_routine_();
          return nullptr;
        },
        this);
    RELEASE_ASSERT(rc == 0, "");

#if SUPPORTS_PTHREAD_NAMING
    // If the name was not specified, get it from the OS. If the name was
    // specified, write it into the thread, and assert that the OS sees it the
    // same way.
    if (name_.empty()) {
      getNameFromOS(name_);
    } else {
      const int set_name_rc = pthread_setname_np(thread_handle_, name_.c_str());
      if (set_name_rc != 0) {
        ENVOY_LOG_MISC(trace, "Error {} setting name `{}'", set_name_rc, name_);
      } else {
        // When compiling in debug mode, read back the thread-name from the OS,
        // and verify it's what we asked for. This ensures the truncation is as
        // expected, and that the OS will actually retain all the bytes of the
        // name we expect.
        //
        // Note that the system-call to read the thread name may fail in case
        // the thread exits after the call to set the name above, and before the
        // call to get the name, so we can only do the assert if that call
        // succeeded.
        std::string check_name;
        ASSERT(!getNameFromOS(check_name) || check_name == name_,
               absl::StrCat("configured name=", name_, " os name=", check_name));
      }
    }
#endif
  }

  ~ThreadImplPosix() override { ASSERT(joined_); }

  std::string name() const override { return name_; }

  // Thread::Thread
  void join() override {
    ASSERT(!joined_);
    joined_ = true;
    const int rc = pthread_join(thread_handle_, nullptr);
    RELEASE_ASSERT(rc == 0, "");
  }

private:
#if SUPPORTS_PTHREAD_NAMING
  // Attempts to get the name from the operating system, returning true and
  // updating 'name' if successful. Note that during normal operation this
  // may fail, if the thread exits prior to the system call.
  bool getNameFromOS(std::string& name) {
    // Verify that the name got written into the thread as expected.
    char buf[PTHREAD_MAX_THREADNAME_LEN_INCLUDING_NULL_BYTE];
    const int get_name_rc = pthread_getname_np(thread_handle_, buf, sizeof(buf));
    if (get_name_rc != 0) {
      ENVOY_LOG_MISC(trace, "Error {} getting name", get_name_rc);
      return false;
    }
    name = buf;
    return true;
  }
#endif

  std::function<void()> thread_routine_;
  pthread_t thread_handle_;
  std::string name_;
  bool joined_{false};
};

ThreadPtr ThreadFactoryImplPosix::createThread(std::function<void()> thread_routine,
                                               OptionsOptConstRef options) {
  return std::make_unique<ThreadImplPosix>(thread_routine, options);
}

ThreadId ThreadFactoryImplPosix::currentThreadId() { return ThreadId(getCurrentThreadId()); }

} // namespace Thread
} // namespace Envoy
