#include "extensions/filters/network/dubbo_proxy/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

bool Utility::isContainWildcard(const std::string& input) {
  return (input.find('*') != std::string::npos) || (input.find('?') != std::string::npos);
}

bool Utility::wildcardMatch(const char* input, const char* pattern) {
  while (*pattern) {
    if (*pattern == '?') {
      if (!*input) {
        return false;
      }

      ++input;
      ++pattern;
    } else if (*pattern == '*') {
      if (wildcardMatch(input, pattern + 1)) {
        return true;
      }

      if (*input && wildcardMatch(input + 1, pattern)) {
        return true;
      }

      return false;
    } else {
      if (*input++ != *pattern++) {
        return false;
      }
    }
  }

  return !*input && !*pattern;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
