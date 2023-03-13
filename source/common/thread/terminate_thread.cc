#include "source/common/thread/terminate_thread.h"

#include <sys/types.h>

#include <csignal>

#include "source/common/common/logger.h"

extern "C" void __gcov_dump();

namespace Envoy {
namespace Thread {
namespace {
#ifdef __linux__
pid_t toPlatformTid(int64_t tid) { return static_cast<pid_t>(tid); }
#elif defined(__APPLE__)
uint64_t toPlatformTid(int64_t tid) { return static_cast<uint64_t>(tid); }
#endif
} // namespace

bool terminateThread(const ThreadId& tid) {
#if defined(ENVOY_CONFIG_COVERAGE)
#if __ELF__
  // Flush coverage if we are in coverage mode.
  if (&__gcov_dump != nullptr) {
    __gcov_dump();
  }
#endif
#endif

#ifndef WIN32
  // Assume POSIX-compatible system and signal to the thread.
  return kill(toPlatformTid(tid.getId()), SIGABRT) == 0;
#else
  // Windows, currently unsupported termination of thread.
  ENVOY_LOG_MISC(error, "Windows is currently unsupported for terminateThread.");
  return false;
#endif
}

} // namespace Thread
} // namespace Envoy
