// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_thread_impl.h"

#include <string>

#include "exe/platform_impl.h"

Envoy::Thread::ThreadFactory* getThreadFactory() {
  static Envoy::PlatformImpl* platform_impl = new Envoy::PlatformImpl();
  return &platform_impl->threadFactory();
}

namespace quic {

QuicThreadImpl::QuicThreadImpl(const std::string& /*name*/) {
  thread_factory_ = getThreadFactory();
}

} // namespace quic
