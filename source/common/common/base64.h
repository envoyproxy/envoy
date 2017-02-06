#pragma once

#include "envoy/buffer/buffer.h"

class Base64 {
public:
  /**
   * Base64 encode an input buffer.
   * @param buffer supplies the buffer to encode.
   * @param length supplies the length to encode which may be <= the buffer length.
   */
  static std::string encode(const Buffer::Instance& buffer, uint64_t length);

  /**
   * Base64 decode an input string.
   * @param input supplies the input to decode.
   *
   * Note, decoded string may contain '\0' at any position, it should be treated as a sequence of
   * bytes.
   */
  static std::string decode(const std::string& input);
};
