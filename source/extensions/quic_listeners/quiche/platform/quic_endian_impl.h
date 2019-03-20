#pragma once

#include <arpa/inet.h>

#include <cstdint>

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

namespace quic {

class QuicEndianImpl {
public:
  static uint16_t HostToNet16(uint16_t x) { return htons(x); }
  static uint32_t HostToNet32(uint32_t x) { return htonl(x); }
  // TODO: implement
  static uint64_t HostToNet64(uint64_t /*x*/) { return 0; }

  static uint16_t NetToHost16(uint16_t x) { return ntohs(x); }
  static uint32_t NetToHost32(uint32_t x) { return ntohl(x); }
  // TODO: implement
  static uint64_t NetToHost64(uint64_t /*x*/) { return 0; }

  // TODO: implement
  static bool HostIsLittleEndian() { return false; }
};

} // namespace quic
