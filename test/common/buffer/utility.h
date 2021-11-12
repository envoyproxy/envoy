#pragma once

#include <initializer_list>

#include "source/common/buffer/buffer_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Buffer {
namespace {

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

} // namespace
} // namespace Buffer
} // namespace Envoy
