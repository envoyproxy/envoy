#include "library/common/apple/utility.h"

namespace Envoy {
namespace Apple {

std::string toString(CFStringRef cf_string) {
  // A pointer to a C string or NULL if the internal storage of string
  // does not allow this to be returned efficiently.
  // CFStringGetCStringPtr will return a pointer to the string with no memory allocation and in
  // constant time, if it can; otherwise, it will return null.
  auto efficient_c_str = CFStringGetCStringPtr(cf_string, kCFStringEncodingUTF8);
  if (efficient_c_str) {
    return std::string(efficient_c_str);
  }

  CFIndex length = CFStringGetLength(cf_string);
  CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char* c_str = static_cast<char*>(malloc(size));
  // Use less efficient method of getting c string if CFStringGetCStringPtr failed.
  std::string ret_str;
  if (CFStringGetCString(cf_string, c_str, size, kCFStringEncodingUTF8)) {
    ret_str = std::string(c_str);
  }

  free(c_str);
  return ret_str;
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
