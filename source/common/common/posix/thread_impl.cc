#include "source/common/common/posix/thread_impl.h"

#include "envoy/thread/thread.h"

#include "source/common/common/assert.h"

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

ThreadHandle::ThreadHandle(std::function<void()> thread_routine)
    : thread_routine_(thread_routine) {}

/** Returns the thread routine. */
std::function<void()>& ThreadHandle::routine() { return thread_routine_; };

/** Returns the thread handle. */
pthread_t& ThreadHandle::handle() { return thread_handle_; }

/**
 * Wrapper for a pthread thread. We don't use std::thread because it eats exceptions and leads to
 * unusable stack traces.
 */
PosixThread::PosixThread(ThreadHandle* thread_handle, OptionsOptConstRef options)
    : thread_handle_(thread_handle) {
  if (options) {
    name_ = options->name_.substr(0, PTHREAD_MAX_THREADNAME_LEN_INCLUDING_NULL_BYTE - 1);
  }

#if SUPPORTS_PTHREAD_NAMING
  // If the name was not specified, get it from the OS. If the name was
  // specified, write it into the thread, and assert that the OS sees it the
  // same way.
  if (name_.empty()) {
    getNameFromOS(name_);
  } else {
    const int set_name_rc = pthread_setname_np(thread_handle_->handle(), name_.c_str());
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

PosixThread::~PosixThread() {
  ASSERT(joined_);
  delete thread_handle_;
}

std::string PosixThread::name() const { return name_; }

// Thread::Thread
void PosixThread::join() {
  ASSERT(!joined_);
  joined_ = true;
  const int rc = pthread_join(thread_handle_->handle(), nullptr);
  RELEASE_ASSERT(rc == 0, "");
}

bool PosixThread::joinable() const { return !joined_; }

ThreadId PosixThread::pthreadId() const {
  ASSERT(!joined_);
#if defined(__linux__)
  return ThreadId(static_cast<int64_t>(thread_handle_->handle()));
#elif defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(thread_handle_->handle(), &tid);
  return ThreadId(tid);
#else
#error "Enable and test pthread id retrieval code for you arch in pthread/thread_impl.cc"
#endif
}

#if SUPPORTS_PTHREAD_NAMING
// Attempts to get the name from the operating system, returning true and
// updating 'name' if successful. Note that during normal operation this
// may fail, if the thread exits prior to the system call.
bool PosixThread::getNameFromOS(std::string& name) {
  // Verify that the name got written into the thread as expected.
  char buf[PTHREAD_MAX_THREADNAME_LEN_INCLUDING_NULL_BYTE] = {0};
  const int get_name_rc = pthread_getname_np(thread_handle_->handle(), buf, sizeof(buf));
  name = buf;
  return get_name_rc == 0;
}
#endif

class PosixThreadFactoryImpl : public PosixThreadFactory {
public:
  ThreadPtr createThread(std::function<void()> thread_routine,
                         OptionsOptConstRef options) override {
    return createThread(thread_routine, options, /* crash_on_failure= */ true);
  };

  PosixThreadPtr createThread(std::function<void()> thread_routine, OptionsOptConstRef options,
                              bool crash_on_failure) override {
    auto thread_handle = new ThreadHandle(thread_routine);
    const int rc = pthread_create(
        &thread_handle->handle(), nullptr,
        [](void* arg) -> void* {
          static_cast<ThreadHandle*>(arg)->routine()();
          return nullptr;
        },
        reinterpret_cast<void*>(thread_handle));
    if (rc != 0) {
      delete thread_handle;
      if (crash_on_failure) {
        RELEASE_ASSERT(false, fmt::format("Unable to create a thread with return code: {}", rc));
      } else {
        IS_ENVOY_BUG(fmt::format("Unable to create a thread with return code: {}", rc));
      }
      return nullptr;
    }
    return std::make_unique<PosixThread>(thread_handle, options);
  };

  ThreadId currentThreadId() override { return ThreadId(getCurrentThreadId()); };

  ThreadId currentPthreadId() override {
#if defined(__linux__)
    return static_cast<ThreadId>(static_cast<int64_t>(pthread_self()));
#elif defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(pthread_self(), &tid);
    return ThreadId(tid);
#else
#error "Enable and test pthread id retrieval code for you arch in pthread/thread_impl.cc"
#endif
  }
};

PosixThreadFactoryPtr PosixThreadFactory::create() {
  return std::make_unique<PosixThreadFactoryImpl>();
}

} // namespace Thread
} // namespace Envoy
