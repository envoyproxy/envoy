#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <sys/socket.h>

#include <cstdint>

#include "extensions/quic_listeners/quiche/platform/quic_ip_address_impl.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_socket_address.h
// This is a dummy implementation which just allows its dependency to build.
// TODO(vasilvv) Remove this impl once QuicSocketAddress and QuicIpAddress are
// removed from platform API.

class QuicSocketAddressImpl {
public:
  QuicSocketAddressImpl() = default;
  QuicSocketAddressImpl(QuicIpAddressImpl, uint16_t) {}
  explicit QuicSocketAddressImpl(const struct sockaddr_storage&) {}
  explicit QuicSocketAddressImpl(const struct sockaddr&) {}
  QuicSocketAddressImpl(const QuicSocketAddressImpl&) = default;
  QuicSocketAddressImpl& operator=(const QuicSocketAddressImpl&) = default;
  QuicSocketAddressImpl& operator=(QuicSocketAddressImpl&&) = default;
  friend bool operator==(QuicSocketAddressImpl, QuicSocketAddressImpl) { return false; }
  friend bool operator!=(QuicSocketAddressImpl, QuicSocketAddressImpl) { return true; }

  bool IsInitialized() const { return false; }
  std::string ToString() const { return "Unimplemented."; }
  int FromSocket(int) { return -1; }
  QuicSocketAddressImpl Normalized() const { return QuicSocketAddressImpl(); }

  QuicIpAddressImpl host() const { return QuicIpAddressImpl(); }
  uint16_t port() const { return 0; }

  sockaddr_storage generic_address() const { return sockaddr_storage{}; }
};

} // namespace quic
