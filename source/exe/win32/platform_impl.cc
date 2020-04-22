#include "common/common/assert.h"
#include "common/common/thread_impl.h"
#include "common/filesystem/filesystem_impl.h"

#include "exe/platform_impl.h"

namespace Envoy {

PlatformImpl::PlatformImpl()
    : thread_factory_(std::make_unique<Thread::ThreadFactoryImplWin32>()),
      file_system_(std::make_unique<Filesystem::InstanceImplWin32>()) {
  WSADATA wsa_data;
  const WORD version_requested = MAKEWORD(2, 2);
  RELEASE_ASSERT(WSAStartup(version_requested, &wsa_data) == 0, "WSAStartup failed with error");
}

PlatformImpl::~PlatformImpl() { ::WSACleanup(); }

} // namespace Envoy
