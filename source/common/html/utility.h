#pragma once

#include <string>

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
  static std::string sanitize(const std::string& text);
};

} // namespace Html
} // namespace Envoy
