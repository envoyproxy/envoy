#pragma once

#include "common/common/assert.h"
#include "common/common/thread_impl.h"

#include "absl/debugging/symbolize.h"

// clang-format off
#include <winsock2.h>
// clang-format on

namespace Envoy {

class PlatformSetup {
public:
  PlatformSetup(int, const char* const* argv) {
    // absl::Symbolize mostly works without this, but this improves corner case
    // handling
    absl::InitializeSymbolizer(argv[0]);

    const WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    const int rc = ::WSAStartup(wVersionRequested, &wsaData);
    RELEASE_ASSERT(rc == 0, "WSAStartup failed with error");
  }

  ~PlatformSetup() { ::WSACleanup(); }

  Thread::ThreadFactory& threadFactory() { return thread_factory_; }

private:
  Thread::ThreadFactoryImplWin32 thread_factory_;
};

} // namespace Envoy