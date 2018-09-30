#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace {

inline void addInt16(Buffer::Instance& buffer, int16_t value) {
  value = htobe16(value);
  buffer.add(&value, 2);
}

inline void addInt32(Buffer::Instance& buffer, int32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt64(Buffer::Instance& buffer, int64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
}

inline void addInt8(Buffer::Instance& buffer, int8_t value) { buffer.add(&value, 1); }

inline void addRepeated(Buffer::Instance& buffer, int n, int8_t value) {
  for (int i = 0; i < n; i++) {
    buffer.add(&value, 1);
  }
}

inline void addSeq(Buffer::Instance& buffer, const std::initializer_list<uint8_t> values) {
  for (int8_t value : values) {
    buffer.add(&value, 1);
  }
}

inline void addString(Buffer::Instance& buffer, const std::string& s) { buffer.add(s); }

} // namespace
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy