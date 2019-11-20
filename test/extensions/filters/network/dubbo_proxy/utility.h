#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"
#include "common/common/byte_order.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

inline void addInt32(Buffer::Instance& buffer, uint32_t value) {
  value = htobe32(value);
  buffer.add(&value, 4);
}

inline void addInt64(Buffer::Instance& buffer, uint64_t value) {
  value = htobe64(value);
  buffer.add(&value, 8);
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy