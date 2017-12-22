#include "common/html/utility.h"

#include <string>

namespace Envoy {
namespace Html {

/* static */
std::string Utility::sanitize(const std::string& text) {
  std::string buffer;
  for (char ch : text) {
    switch (ch) {
    case '&':
      buffer.append("&amp;");
      break;
    case '\"':
      buffer.append("&quot;");
      break;
    case '\'':
      buffer.append("&apos;");
      break;
    case '<':
      buffer.append("&lt;");
      break;
    case '>':
      buffer.append("&gt;");
      break;
    default:
      buffer.append(1, ch);
      break;
    }
  }
  return buffer;
}

} // namespace Html
} // namespace Envoy
