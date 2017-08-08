#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"

namespace Envoy {
class Base64 {
public:
  /**
   * Base64 encode an input buffer.
   * @param buffer supplies the buffer to encode.
   * @param length supplies the length to encode which may be <= the buffer length.
   */
  static std::string encode(const Buffer::Instance& buffer, uint64_t length);

  /**
   * Base64 encode an input char buffer with a given length.
   * @param input char array to encode.
   * @param length of the input array.
   */
  static std::string encode(const char* input, uint64_t length);

  /**
   * Base64 decode an input string.
   * @param input supplies the input to decode.
   *
   * Note, decoded string may contain '\0' at any position, it should be treated as a sequence of
   * bytes.
   */
  static std::string decode(const std::string& input);

private:
  /**
   * Helper method for encoding. This is used to encode all of the characters from the input string.
   */
  static void encodeBase(const uint8_t cur_char, uint64_t pos, uint8_t& next_c, std::string& ret);

  /**
   * Encode last characters. It appends '=' chars to the ret if input
   * string length is not divisible by 3.
   */
  static void encodeLast(uint64_t pos, uint8_t last_char, std::string& ret);
};
} // namespace Envoy
