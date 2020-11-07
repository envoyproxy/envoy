#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/thread_impl.h"
#include "common/event/signal_impl.h"
#include "common/filesystem/filesystem_impl.h"

#include "exe/platform_impl.h"

namespace Envoy {

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
  auto eventBridgeHandlers = Event::eventBridgeHandlersSingleton::get();
  auto handler_it = eventBridgeHandlers.find(fdwCtrlType);
  if (handler_it == eventBridgeHandlers.end() || !handler_it->second) {
    return 0;
  }

  Buffer::OwnedImpl buffer;
  constexpr absl::string_view data{"a"};
  buffer.add(data);
  auto result = handler_it->second->write(buffer);
  RELEASE_ASSERT(result.rc_ == 1,
                 fmt::format("failed to write 1 byte: {}", result.err_->getErrorDetails()));
  return 1;
}

PlatformImpl::PlatformImpl()
    : thread_factory_(std::make_unique<Thread::ThreadFactoryImplWin32>()),
      file_system_(std::make_unique<Filesystem::InstanceImplWin32>()) {
  WSADATA wsa_data;
  const WORD version_requested = MAKEWORD(2, 2);
  RELEASE_ASSERT(WSAStartup(version_requested, &wsa_data) == 0, "WSAStartup failed with error");

  if (!SetConsoleCtrlHandler(CtrlHandler, 1)) {
    ENVOY_LOG_MISC(warn, "Could not set Windows Control Handlers. Continuing without them.");
  }
}

PlatformImpl::~PlatformImpl() { ::WSACleanup(); }

} // namespace Envoy
