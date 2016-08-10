#pragma once

#include "envoy/buffer/buffer.h"

class Base64 {
public:
  /**
   * base 64 encode an input buffer.
   * @param buffer supplies the buffer to encode.
   * @param length supplies the length to encode which may be <= the buffer length.
   */
  static std::string encode(const Buffer::Instance& buffer, uint64_t length);
};
