#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <netinet/in.h>

#include <string>

#include "quiche/quic/platform/api/quic_ip_address_family.h"

namespace quic {

// Implements the interface required by
// https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/quic_ip_address.h
// This is a dummy implementation which just allows its dependency to build.
// TODO(vasilvv) Remove this impl once QuicSocketAddress and QuicIpAddress are
// removed from platform API.

class QuicIpAddressImpl {
public:
  enum : size_t { kIPv4AddressSize = sizeof(in_addr), kIPv6AddressSize = sizeof(in6_addr) };
  static QuicIpAddressImpl Loopback4() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Loopback6() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Any4() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Any6() { return QuicIpAddressImpl(); }

  QuicIpAddressImpl() = default;
  QuicIpAddressImpl(const in_addr&) {}
  QuicIpAddressImpl(const in6_addr&) {}
  QuicIpAddressImpl(const QuicIpAddressImpl& other) = default;
  QuicIpAddressImpl& operator=(const QuicIpAddressImpl& other) = default;
  QuicIpAddressImpl& operator=(QuicIpAddressImpl&& other) = default;
  friend bool operator==(QuicIpAddressImpl, QuicIpAddressImpl) { return false; }
  friend bool operator!=(QuicIpAddressImpl, QuicIpAddressImpl) { return true; }

  bool IsInitialized() const { return false; }
  IpAddressFamily address_family() const { return IpAddressFamily::IP_V4; }
  int AddressFamilyToInt() const { return 4; }
  std::string ToPackedString() const { return "Unimplemented"; }
  std::string ToString() const { return "Unimplemented"; }
  QuicIpAddressImpl Normalized() const { return QuicIpAddressImpl(); }
  QuicIpAddressImpl DualStacked() const { return QuicIpAddressImpl(); }
  bool FromPackedString(const char*, size_t) { return false; }
  bool FromString(const std::string&) { return false; }
  bool IsIPv4() const { return true; }
  bool IsIPv6() const { return false; }
  in_addr GetIPv4() const { return in_addr(); }
  in6_addr GetIPv6() const { return in6_addr(); }
  bool InSameSubnet(const QuicIpAddressImpl&, int) { return false; }
};

} // namespace quic
