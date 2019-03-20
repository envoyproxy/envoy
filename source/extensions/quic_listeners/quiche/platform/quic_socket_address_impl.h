#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.


namespace quic {

class QuicSocketAddressImpl {
 public:
  QuicSocketAddressImpl() = default;
  explicit QuicSocketAddressImpl(const struct sockaddr_storage& saddr)  { return QuicSocketAddressImpl(); }
  explicit QuicSocketAddressImpl(const struct sockaddr& saddr)  { return QuicSocketAddressImpl(); }
  QuicSocketAddressImpl(const QuicSocketAddressImpl& other) = default;
  QuicSocketAddressImpl& operator=(const QuicSocketAddressImpl& other) =
      default;
  QuicSocketAddressImpl& operator=(QuicSocketAddressImpl&& other) = default;
  friend bool operator==(QuicSocketAddressImpl lhs, QuicSocketAddressImpl rhs) { return false; }
  friend bool operator!=(QuicSocketAddressImpl lhs, QuicSocketAddressImpl rhs) { return true; }

  bool IsInitialized() const { return false; }
  std::string ToString() const { return "Unimplemented.";}
  int FromSocket(int fd) { return -1; }
  QuicSocketAddressImpl Normalized() const { return QuicSocketAddressImpl(); }

  QuicIpAddressImpl host() const { return QuicIpAddressImpl(); }
  uint16_t port() const { return 0; }

  sockaddr_storage generic_address() const {
    return sockaddr_storage();
  }
};

}  // namespace quic
