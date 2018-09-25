#include "extensions/filters/network/dubbo_proxy/buffer_helper.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
double BufferHelper::peekDouble(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 8) {
    throw EnvoyException("buffer underflow");
  }
  double i;
  uint64_t j = buffer.peekBEInt<uint64_t>(offset);
  std::memcpy(&i, &j, 8);
  return i;
}

float BufferHelper::peekFloat(Buffer::Instance& buffer, uint64_t offset) {
  if (buffer.length() < offset + 4) {
    throw EnvoyException("buffer underflow");
  }
  float i;
  uint32_t j = buffer.peekBEInt<uint32_t>(offset);
  std::memcpy(&i, &j, 4);
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
  uint64_t i = buffer.drainBEInt<uint64_t>();
  double d;
  std::memcpy(&d, &i, 8);
  return d;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
