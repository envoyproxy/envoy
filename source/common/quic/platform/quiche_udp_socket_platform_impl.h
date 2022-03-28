#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sys/socket.h>

namespace quiche {

const size_t kCmsgSpaceForGooglePacketHeaderImpl = 0;

// NOLINTNEXTLINE(readability-identifier-naming)
inline bool GetGooglePacketHeadersFromControlMessageImpl(struct ::cmsghdr* /*cmsg*/,
                                                         char** /*packet_headers*/,
                                                         size_t* /*packet_headers_len*/) {
  return false;
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void SetGoogleSocketOptionsImpl(int /*fd*/) {}

} // namespace quiche
