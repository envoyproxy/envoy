#include "extensions/filters/network/thrift_proxy/buffer_helper.h"

#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

int8_t BufferHelper::peekI8(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 1);
  int8_t i;
  buffer.copyOut(offset, 1, &i);
  return i;
}

int16_t BufferHelper::peekI16(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 2);
  int16_t i;
  buffer.copyOut(offset, 2, &i);
  return be16toh(i);
}

int32_t BufferHelper::peekI32(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 4);
  int32_t i;
  buffer.copyOut(offset, 4, &i);
  return be32toh(i);
}

int64_t BufferHelper::peekI64(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 8);
  int64_t i;
  buffer.copyOut(offset, 8, &i);
  return be64toh(i);
}

uint16_t BufferHelper::peekU16(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 2);
  uint16_t i;
  buffer.copyOut(offset, 2, &i);
  return be16toh(i);
}

uint32_t BufferHelper::peekU32(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 4);
  uint32_t i;
  buffer.copyOut(offset, 4, &i);
  return be32toh(i);
}

uint64_t BufferHelper::peekU64(Buffer::Instance& buffer, size_t offset) {
  ASSERT(buffer.length() >= 8);
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
  ASSERT(sizeof(double) == sizeof(uint64_t));
  ASSERT(std::numeric_limits<double>::is_iec559);

  // Cribbed from protobuf WireFormatLite.
  union {
    double d;
    uint64_t i;
  };
  i = drainU64(buffer);
  return d;
}

// Thrift's var int encoding is described in
// https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
uint64_t BufferHelper::peekVarInt(Buffer::Instance& buffer, size_t offset, int& size) {
  ASSERT(buffer.length() >= offset + 1);

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

int32_t BufferHelper::peekVarIntI32(Buffer::Instance& buffer, size_t offset, int& size) {
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
int64_t BufferHelper::peekZigZagI64(Buffer::Instance& buffer, size_t offset, int& size) {
  int underlying_size;
  uint64_t zz64 = peekVarInt(buffer, offset, underlying_size);
  ASSERT(underlying_size != 0);

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
int32_t BufferHelper::peekZigZagI32(Buffer::Instance& buffer, size_t offset, int& size) {
  int underlying_size;
  uint64_t zz64 = peekVarInt(buffer, offset, underlying_size);
  ASSERT(underlying_size != 0);

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

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
