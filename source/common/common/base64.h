#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * A utility class to support base64 encoding, which is defined in RFC4648 Section 4.
 * See https://tools.ietf.org/html/rfc4648#section-4
 */
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
   * Base64 encode an input char buffer with a given length.
   * @param input char array to encode.
   * @param length of the input array.
   * @param whether add padding at the end of the output.
   */
  static std::string encode(const char* input, uint64_t length, bool add_padding);

  /**
   * Base64 decode an input string. Padding is required.
   * @param input supplies the input to decode.
   *
   * Note, decoded string may contain '\0' at any position, it should be treated as a sequence of
   * bytes.
   */
  static std::string decode(absl::string_view input);

  /**
   * Base64 decode an input string. Padding is not required.
   * @param input supplies the input to decode.
   *
   * Note, decoded string may contain '\0' at any position, it should be treated as a sequence of
   * bytes.
   */
  static std::string decodeWithoutPadding(absl::string_view input);

  /**
   * Add the padding in the base64 encoded binary if the padding is missing.
   * @param encoded is the target to complete the padding.
   */
  static void completePadding(std::string& encoded);
};

/**
 * A utility class to support base64url encoding, which is defined in RFC4648 Section 5.
 * See https://tools.ietf.org/html/rfc4648#section-5
 */
class Base64Url {
public:
  /**
   * Base64url encode an input char buffer with a given length.
   * @param input char array to encode.
   * @param length of the input array.
   */
  static std::string encode(const char* input, uint64_t length);

  /**
   * Base64url decode an input string. Padding must not be included in the input.
   * @param input supplies the input to decode.
   *
   * Note, decoded string may contain '\0' at any position, it should be treated as a sequence of
   * bytes.
   */
  static std::string decode(absl::string_view input);
};

} // namespace Envoy
