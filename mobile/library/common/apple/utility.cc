#include "library/common/apple/utility.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Apple {

std::string toString(CFStringRef cf_string) {
  if (cf_string == nullptr) {
    return std::string();
  }

  // A pointer to a C string or NULL if the internal storage of string
  // does not allow this to be returned efficiently.
  // CFStringGetCStringPtr will return a pointer to the string with no memory allocation and in
  // constant time, if it can; otherwise, it will return null.
  const char* efficient_c_str = CFStringGetCStringPtr(cf_string, kCFStringEncodingUTF8);
  if (efficient_c_str) {
    return std::string(efficient_c_str);
  }

  CFIndex length = CFStringGetLength(cf_string);
  if (length == 0) {
    return std::string();
  }

  // Adding 1 to accommodate the `\0` null delimiter in a C string.
  CFIndex size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char* c_str = static_cast<char*>(malloc(size));
  // Use less efficient method of getting c string if CFStringGetCStringPtr failed.
  const bool ret = CFStringGetCString(cf_string, c_str, size, kCFStringEncodingUTF8);
  ENVOY_BUG(ret, "CFStringGetCString failed to convert CFStringRef to C string.");

  std::string ret_str(c_str);

  free(c_str);
  return ret_str;
}

int toInt(CFNumberRef number) {
  if (number == nullptr) {
    return 0;
  }

  int value = 0;
  const bool ret = CFNumberGetValue(number, kCFNumberSInt32Type, &value);
  ENVOY_BUG(ret, "CFNumberGetValue failed to convert CFNumberRef to int.");

  return value;
}

} // namespace Apple
} // namespace Envoy
