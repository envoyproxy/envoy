// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_hostname_utils_impl.h"

#include <string>

// TODO(wub): Implement both functions on top of GoogleUrl, then enable
// quiche/quic/platform/api/quic_hostname_utils_test.cc.

namespace quic {

// static
bool QuicHostnameUtilsImpl::IsValidSNI(QuicStringPiece sni) {
  // TODO(wub): Implement it on top of GoogleUrl, once it is available.
  return sni.find_last_of('.') != std::string::npos;
}

// static
QuicString QuicHostnameUtilsImpl::NormalizeHostname(QuicStringPiece hostname) {
  // TODO(wub): Implement it on top of GoogleUrl, once it is available.
  QuicString host(hostname);

  // Walk backwards over the string, stopping at the first trailing dot.
  size_t host_end = host.length();
  while (host_end != 0 && host[host_end - 1] == '.') {
    host_end--;
  }

  // Erase the trailing dots.
  if (host_end != host.length()) {
    host.erase(host_end, host.length() - host_end);
  }

  return host;
}

} // namespace quic
