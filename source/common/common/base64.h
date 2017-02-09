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
   * Base64 encode an input string.
   * @param input string to encode
   */
  static std::string encode(const std::string& input);

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
   * Helper methods for encoding.
   */
  static inline void encode_base(const uint8_t cur_char, uint64_t pos, uint8_t& next_c,
                                 std::string& ret);
  /**
   * Encode last characters if input string length is not divisible by 3.
   */
  static inline void encode_last(uint64_t pos, uint8_t last_char, std::string& ret);
};
