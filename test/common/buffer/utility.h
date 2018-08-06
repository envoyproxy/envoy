#pragma once

#include <initializer_list>

#include "common/buffer/buffer_impl.h"

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

inline void addString(Buffer::Instance& buffer, const std::string& s) { buffer.add(s); }

inline std::string bufferToString(Buffer::Instance& buffer) {
  if (buffer.length() == 0) {
    return "";
  }

  char* data = static_cast<char*>(buffer.linearize(buffer.length()));
  return std::string(data, buffer.length());
}

} // namespace
} // namespace Buffer
} // namespace Envoy
