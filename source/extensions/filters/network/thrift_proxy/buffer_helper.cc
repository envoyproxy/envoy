#include "extensions/filters/network/thrift_proxy/buffer_helper.h"

#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

int8_t BufferHelper::peekI8(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 1) {
    throw EnvoyException("buffer underflow");
  }

  int8_t i;
  buffer.copyOut(offset, 1, &i);
  return i;
}

int16_t BufferHelper::peekI16(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 2) {
    throw EnvoyException("buffer underflow");
  }

  int16_t i;
  buffer.copyOut(offset, 2, &i);
  return be16toh(i);
}

int32_t BufferHelper::peekI32(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 4) {
    throw EnvoyException("buffer underflow");
  }

  int32_t i;
  buffer.copyOut(offset, 4, &i);
  return be32toh(i);
}

int64_t BufferHelper::peekI64(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 8) {
    throw EnvoyException("buffer underflow");
  }

  int64_t i;
  buffer.copyOut(offset, 8, &i);
  return be64toh(i);
}

uint16_t BufferHelper::peekU16(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 2) {
    throw EnvoyException("buffer underflow");
  }

  uint16_t i;
  buffer.copyOut(offset, 2, &i);
  return be16toh(i);
}

uint32_t BufferHelper::peekU32(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 4) {
    throw EnvoyException("buffer underflow");
  }

  uint32_t i;
  buffer.copyOut(offset, 4, &i);
  return be32toh(i);
}

uint64_t BufferHelper::peekU64(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 8) {
    throw EnvoyException("buffer underflow");
  }

  uint64_t i;
  buffer.copyOut(offset, 8, &i);
  return be64toh(i);
}

int8_t BufferHelper::drainI8(Buffer::Instance& buffer) {
  int8_t i = peekI8(buffer);
  buffer.drain(1);
  return i;
}

int16_t BufferHelper::drainI16(Buffer::Instance& buffer) {
  int16_t i = peekI16(buffer);
  buffer.drain(2);
  return i;
}

int32_t BufferHelper::drainI32(Buffer::Instance& buffer) {
  int32_t i = peekI32(buffer);
  buffer.drain(4);
  return i;
}

int64_t BufferHelper::drainI64(Buffer::Instance& buffer) {
  int64_t i = peekI64(buffer);
  buffer.drain(8);
  return i;
}

uint32_t BufferHelper::drainU32(Buffer::Instance& buffer) {
  uint32_t i = peekU32(buffer);
  buffer.drain(4);
  return i;
}

uint64_t BufferHelper::drainU64(Buffer::Instance& buffer) {
  uint64_t i = peekU64(buffer);
  buffer.drain(8);
  return i;
}

double BufferHelper::drainDouble(Buffer::Instance& buffer) {
  static_assert(sizeof(double) == sizeof(uint64_t), "sizeof(double) != sizeof(uint64_t)");
  static_assert(std::numeric_limits<double>::is_iec559, "non-IEC559 (IEEE 754) double");

  // Implementation based on:
  // https://github.com/CppCon/CppCon2017/raw/master/Presentations/Type%20Punning%20In%20C%2B%2B17%20-%20Avoiding%20Pun-defined%20Behavior/Type%20Punning%20In%20C%2B%2B17%20-%20Avoiding%20Pun-defined%20Behavior%20-%20Scott%20Schurr%20-%20CppCon%202017.pdf
  // The short version:
  // 1. Reinterpreting uint64_t* to double* falls astray of strict aliasing rules.
  // 2. Using union {uint64_t i; double d;} is undefined behavior in C++ (but not C11).
  // 3. Using memcpy may be undefined, but probably reliable, and can be optimized to the
  //    same instructions as 1 and 2.
  // 4. Implementation of last resort is to manually copy from i to d via unsigned char*.
  uint64_t i = drainU64(buffer);
  double d;
  std::memcpy(&d, &i, 8);
  return d;
}

// Thrift's var int encoding is described in
// https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
uint64_t BufferHelper::peekVarInt(Buffer::Instance& buffer, uint64_t offset, int& size) {
  // Need at least 1 byte for a var int.
  if (buffer.length() <= offset) {
    throw EnvoyException("buffer underflow");
  }

  // Need at most 10 bytes for a 64-bit var int.
  const uint64_t last = std::min(buffer.length() - offset, static_cast<uint64_t>(10));

  uint8_t shift = 0;
  uint64_t result = 0;
  for (uint64_t i = 0; i < last; i++) {
    uint8_t b = peekI8(buffer, offset + i);

    // Note: the compact protocol spec says these variable-length ints are encoded as big-endian,
    // but the Apache C++, Java, and Python implementations read and write them little-endian.
    result |= static_cast<uint64_t>(b & 0x7f) << shift;
    shift += 7;

    if ((b & 0x80) == 0) {
      // End of encoded int.
      size = i + 1;
      return result;
    }
  }

  // Ran out of bytes (or it's invalid).
  size = -last;
  return 0;
}

int32_t BufferHelper::peekVarIntI32(Buffer::Instance& buffer, uint64_t offset, int& size) {
  int underlying_size;
  uint64_t v64 = peekVarInt(buffer, offset, underlying_size);

  if (underlying_size <= -5 || underlying_size > 5) {
    throw EnvoyException("invalid compact protocol varint i32");
  }

  size = underlying_size;
  if (size < 0) {
    return 0;
  }

  return static_cast<int32_t>(v64);
}

// Thrift's zig-zag int encoding is described in
// https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
int64_t BufferHelper::peekZigZagI64(Buffer::Instance& buffer, uint64_t offset, int& size) {
  int underlying_size;
  uint64_t zz64 = peekVarInt(buffer, offset, underlying_size);

  if (underlying_size <= -10 || underlying_size > 10) {
    // Max size is 10, so this must be an invalid encoding.
    throw EnvoyException("invalid compact protocol zig-zag i64");
  }

  size = underlying_size;
  if (size < 0) {
    // Still an underflow, but it might become valid with additional data.
    return 0;
  }

  return (zz64 >> 1) ^ static_cast<uint64_t>(-static_cast<int64_t>(zz64 & 1));
}

// Thrift's zig-zag int encoding is described in
// https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
int32_t BufferHelper::peekZigZagI32(Buffer::Instance& buffer, uint64_t offset, int& size) {
  int underlying_size;
  uint64_t zz64 = peekVarInt(buffer, offset, underlying_size);

  if (underlying_size <= -5 || underlying_size > 5) {
    // Max size is 5, so this must be an invalid encoding.
    throw EnvoyException("invalid compact protocol zig-zag i32");
  }

  size = underlying_size;
  if (size < 0) {
    // Still an underflow, but it might become valid with additional data.
    return 0;
  }

  uint32_t zz32 = static_cast<uint32_t>(zz64);
  return (zz32 >> 1) ^ static_cast<uint32_t>(-static_cast<int32_t>(zz32 & 1));
}

void BufferHelper::writeI8(Buffer::Instance& buffer, int8_t value) { buffer.add(&value, 1); }

void BufferHelper::writeI16(Buffer::Instance& buffer, int16_t value) {
  value = htobe16(value);
  buffer.add(&value, 2);
}

void BufferHelper::writeU16(Buffer::Instance& buffer, uint16_t value) {
  value = htobe16(value);
  buffer.add(&value, 2);
}

void BufferHelper::writeI32(Buffer::Instance& buffer, int32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

void BufferHelper::writeU32(Buffer::Instance& buffer, uint32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

void BufferHelper::writeI64(Buffer::Instance& buffer, int64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
}

void BufferHelper::writeDouble(Buffer::Instance& buffer, double value) {
  static_assert(sizeof(double) == sizeof(uint64_t), "sizeof(double) != sizeof(uint64_t)");
  static_assert(std::numeric_limits<double>::is_iec559, "non-IEC559 (IEEE 754) double");

  // See drainDouble for implementation details.
  uint64_t i;
  std::memcpy(&i, &value, 8);
  i = htobe64(i);
  buffer.add(&i, 8);
}

// Thrift's var int encoding is described in
// https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
void BufferHelper::writeVarIntI32(Buffer::Instance& buffer, int32_t value) {
  uint8_t bytes[5];
  uint32_t v = static_cast<uint32_t>(value);
  int pos = 0;
  while (pos < 5) {
    if ((v & ~0x7F) == 0) {
      bytes[pos++] = static_cast<uint8_t>(v);
      break;
    }

    bytes[pos++] = static_cast<uint8_t>(v & 0x7F) | 0x80;
    v >>= 7;
  }
  ASSERT(v < 0x80);
  ASSERT(pos <= 5);

  buffer.add(bytes, pos);
}

void BufferHelper::writeVarIntI64(Buffer::Instance& buffer, int64_t value) {
  uint8_t bytes[10];
  uint64_t v = static_cast<uint64_t>(value);
  int pos = 0;
  while (pos < 10) {
    if ((v & ~0x7F) == 0) {
      bytes[pos++] = static_cast<uint8_t>(v);
      break;
    }

    bytes[pos++] = static_cast<uint8_t>(v & 0x7F) | 0x80;
    v >>= 7;
  }

  ASSERT(v < 0x80);
  ASSERT(pos <= 10);

  buffer.add(bytes, pos);
}

void BufferHelper::writeZigZagI32(Buffer::Instance& buffer, int32_t value) {
  uint32_t zz32 = (static_cast<uint32_t>(value) << 1) ^ (value >> 31);
  writeVarIntI32(buffer, zz32);
}

void BufferHelper::writeZigZagI64(Buffer::Instance& buffer, int64_t value) {
  uint64_t zz64 = (static_cast<uint64_t>(value) << 1) ^ (value >> 63);
  writeVarIntI64(buffer, zz64);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
