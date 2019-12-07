#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <cstdint>

#include "common/common/byte_order.h"

namespace quiche {

class QuicheEndianImpl {
public:
  static uint16_t HostToNet16(uint16_t x) { return toEndianness<ByteOrder::BigEndian>(x); }
  static uint32_t HostToNet32(uint32_t x) { return toEndianness<ByteOrder::BigEndian>(x); }
  static uint64_t HostToNet64(uint64_t x) { return toEndianness<ByteOrder::BigEndian>(x); }

  static uint16_t NetToHost16(uint16_t x) { return fromEndianness<ByteOrder::BigEndian>(x); }
  static uint32_t NetToHost32(uint32_t x) { return fromEndianness<ByteOrder::BigEndian>(x); }
  static uint64_t NetToHost64(uint64_t x) { return fromEndianness<ByteOrder::BigEndian>(x); }

  static bool HostIsLittleEndian() { return NetToHost16(0x1234) != 0x1234; }
};

} // namespace quiche
