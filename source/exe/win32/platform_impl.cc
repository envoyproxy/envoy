#include <chrono>
#include <thread>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/thread_impl.h"
#include "source/common/event/signal_impl.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/exe/platform_impl.h"

namespace Envoy {

static std::atomic<bool> shutdown_pending = false;

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
  if (shutdown_pending) {
    return 0;
  }
  shutdown_pending = true;

  auto handler = Event::eventBridgeHandlersSingleton::get()[ENVOY_SIGTERM];
  if (!handler) {
    return 0;
  }

  // This code is executed as part of a thread running under a thread owned and
  // managed by Windows console host. For that reason we want to avoid allocating
  // substantial amount of memory or taking locks.
  // This is why we write to a socket to wake up the signal handler.
  char data[] = {'a'};
  Buffer::RawSlice buffer{data, 1};
  auto result = handler->writev(&buffer, 1);
  RELEASE_ASSERT(result.return_value_ == 1,
                 fmt::format("failed to write 1 byte: {}", result.err_->getErrorDetails()));

  if (fdwCtrlType == CTRL_LOGOFF_EVENT || fdwCtrlType == CTRL_SHUTDOWN_EVENT) {
    // These events terminate the process immediately so we want to give a couple of seconds
    // to the dispatcher to shutdown the server.
    constexpr size_t delay = 3;
    absl::SleepFor(absl::Seconds(delay));
  }
  return 1;
}

PlatformImpl::PlatformImpl()
    : thread_factory_(std::make_unique<Thread::ThreadFactoryImplWin32>()),
      file_system_(std::make_unique<Filesystem::InstanceImplWin32>()) {
  WSADATA wsa_data;
  const WORD version_requested = MAKEWORD(2, 2);
  RELEASE_ASSERT(WSAStartup(version_requested, &wsa_data) == 0, "WSAStartup failed with error");

  if (!SetConsoleCtrlHandler(CtrlHandler, 1)) {
    // The Control Handler is executing in a different thread.
    ENVOY_LOG_MISC(warn, "Could not set Windows Control Handlers. Continuing without them.");
  }
}

PlatformImpl::~PlatformImpl() { ::WSACleanup(); }

bool PlatformImpl::enableCoreDump() { return false; }

} // namespace Envoy
