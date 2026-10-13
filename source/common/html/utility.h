#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Html {

/**
 * General HTML utilities.
 */
class Utility {
public:
  /**
   * Sanitizes arbitrary text so it can be included in HTML.
   * @param text arbitrary text to be escaped for safe inclusion in HTML.
   */
  static std::string sanitize(absl::string_view text);

  /**
   * Checks if the text contains any characters that require HTML sanitization.
   * @param text the text to check.
   * @return true if the text contains HTML special characters (&, <, >, ", or '), false otherwise.
   */
  static inline bool requiresSanitization(absl::string_view text) {
    return text.find_first_of("&<>\"'") != absl::string_view::npos;
  }
};

} // namespace Html
} // namespace Envoy
