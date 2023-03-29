#include "library/common/apple/utility.h"

namespace Envoy {
namespace Apple {

std::string toString(CFStringRef string) {
  // A pointer to a C string or NULL if the internal storage of string
  // does not allow this to be returned efficiently.
  auto efficient_c_str = CFStringGetCStringPtr(string, kCFStringEncodingUTF8);
  if (efficient_c_str) {
    return std::string(efficient_c_str);
  }

  CFIndex length = CFStringGetLength(string);
  CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char* c_str = static_cast<char*>(malloc(size));
  // Use less efficient method of getting c string if the most performant one failed
  if (CFStringGetCString(string, c_str, size, kCFStringEncodingUTF8)) {
    const auto ret = std::string(c_str);
    free(c_str);
    return ret;
  }

  return "";
}

int toInt(CFNumberRef number) {
  if (number == nullptr) {
    return 0;
  }

  int value;
  CFNumberGetValue(number, kCFNumberSInt64Type, &value);
  return value;
}

} // namespace Apple
} // namespace Envoy
