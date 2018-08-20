#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

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

uint8_t BufferHelper::peekU8(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 1) {
    throw EnvoyException("buffer underflow");
  }

  uint8_t i;
  buffer.copyOut(offset, 1, &i);
  return i;
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

double BufferHelper::peekDouble(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 8) {
    throw EnvoyException("buffer underflow");
  }
  double i;
  uint64_t j = peekU64(buffer, offset);
  std::memcpy(&i, &j, 8);
  return i;
}

float BufferHelper::peekFloat(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 4) {
    throw EnvoyException("buffer underflow");
  }
  float i;
  uint32_t j = peekU32(buffer, offset);
  std::memcpy(&i, &j, 4);
  return i;
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

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
