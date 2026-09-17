#include "source/common/thread/signal_thread.h"

#include <sys/types.h>

#include <csignal>
#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "source/common/common/logger.h"
#include "source/common/common/macros.h"

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
#ifndef WIN32
  // Assume POSIX-compatible system and signal to the thread.
  return kill(toPlatformTid(tid.getId()), SIGABRT) == 0;
#else
  // Windows, currently unsupported termination of thread.
  ENVOY_LOG_MISC(error, "Windows is currently unsupported for terminateThread.");
  return false;
#endif
}

bool signalThread(const ThreadId& tid, int signal) {
#ifdef __linux__
  return syscall(SYS_tgkill, getpid(), toPlatformTid(tid.getId()), signal) == 0;
#else
  // Only Linux supports the tgkill system call.
  UNREFERENCED_PARAMETER(tid);
  UNREFERENCED_PARAMETER(signal);
  ENVOY_LOG_MISC(error, "signalThread is only supported on Linux.");
  return false;
#endif
}

} // namespace Thread
} // namespace Envoy
