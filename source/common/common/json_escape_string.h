#pragma once

#include <cstdint>
#include <string>

#include "common/common/macros.h"

#include "absl/strings/string_view.h"

namespace Envoy {

// The JSON string escape implementation is taken from:
// https://github.com/nlohmann/json/blob/ec7a1d834773f9fee90d8ae908a0c9933c5646fc/src/json.hpp#L4604-L4697.
// Assumption: the input string will be ASCII only. This is here to reduce dependencies that
// minimal_logger_lib maintains.
class JsonEscaper {
public:
  // Escape a string by replacing certain special characters by a sequence of an escape character
  // (backslash) and another character and other control characters by a sequence of "\u" followed
  // by a four-digit hex representation (https://tools.ietf.org/html/rfc7159#page-8).
  // @param input input string.
  // @return std::string JSON escaped string.
  static std::string escapeString(absl::string_view input, uint64_t required_size) {
    // Create a result string of necessary size.
    std::string result(input.size() + required_size, '\\');
    uint64_t position = 0;

    for (const auto& character : input) {
      switch (character) {
      // Quotation mark (0x22).
      case '"': {
        result[position + 1] = '"';
        position += 2;
        break;
      }

      // Reverse solidus (0x5c).
      case '\\': {
        // Nothing to change.
        position += 2;
        break;
      }

      // Backspace (0x08).
      case '\b': {
        result[position + 1] = 'b';
        position += 2;
        break;
      }

      // Form feed (0x0c).
      case '\f': {
        result[position + 1] = 'f';
        position += 2;
        break;
      }

      // Newline (0x0a).
      case '\n': {
        result[position + 1] = 'n';
        position += 2;
        break;
      }

      // Carriage return (0x0d).
      case '\r': {
        result[position + 1] = 'r';
        position += 2;
        break;
      }

      // Horizontal tab (0x09).
      case '\t': {
        result[position + 1] = 't';
        position += 2;
        break;
      }

      default: {
        if (character >= 0x00 and character <= 0x1f) {
          // Print character as unicode hex.
          sprintf(&result[position + 1], "u%04x", int(character));
          position += 6;
          // Overwrite trailing null character.
          result[position] = '\\';
        } else {
          // All other characters are added as-is.
          result[position++] = character;
        }
        break;
      }
      }
    }

    return result;
  }

  // Calculates the extra space to build a JSON escaped string.
  // @param input input string.
  // @return uint64_t the number of extra characters required to to build a JSON escaped string.
  static uint64_t extraSpace(absl::string_view input) {
    uint64_t result = 0;
    for (const auto& character : input) {
      switch (character) {
      case '"':
        FALLTHRU;
      case '\\':
        FALLTHRU;
      case '\b':
        FALLTHRU;
      case '\f':
        FALLTHRU;
      case '\n':
        FALLTHRU;
      case '\r':
        FALLTHRU;
      case '\t': {
        // From character (1 byte) to hex (2 bytes).
        result += 1;
        break;
      }

      default: {
        if (character >= 0x00 and character <= 0x1f) {
          // From character (1 byte) to unicode hex (6 bytes).
          result += 5;
        }
        break;
      }
      }
    }
    return result;
  }
};

} // namespace Envoy
