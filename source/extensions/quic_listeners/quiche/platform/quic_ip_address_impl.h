#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

class QuicIpAddressImpl {
 public:
  static QuicIpAddressImpl Loopback4() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Loopback6() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Any4() { return QuicIpAddressImpl(); }
  static QuicIpAddressImpl Any6() { return QuicIpAddressImpl(); }

  bool IsInitialized() const { return false; }
  IpAddressFamily address_family() const { return IP_V4; }
  int AddressFamilyToInt() const { return 4; }
  std::string ToPackedString() const { return "Unimplemented"; }
  std::string ToString() const { return "Unimplemented"; }
  QuicIpAddressImpl Normalized() const { return QuicIpAddressImpl(); }
  QuicIpAddressImpl DualStacked() const { return QuicIpAddressImpl(); }
  bool FromPackedString(const char* data, size_t length) { return false; }
  bool FromString(std::string str) { return false; }
  bool IsIPv4() const { return true; }
  bool IsIPv6() const { return false; }

  bool InSameSubnet(const QuicIpAddressImpl& other, int subnet_length) { return false; }
};

}  // namespace quic
