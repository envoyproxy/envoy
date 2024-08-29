#include "gtest/gtest.h"
#include "library/common/apple/utility.h"

namespace Envoy {
namespace Apple {

TEST(AppleUtilityTest, ToString) {
  CFStringRef cf_string = CFSTR("hello, world");
  ASSERT_EQ("hello, world", Apple::toString(cf_string));
  CFRelease(cf_string);
}

TEST(AppleUtilityTest, ToStringEmpty) {
  CFStringRef cf_string = CFSTR("");
  ASSERT_EQ("", Apple::toString(cf_string));
  CFRelease(cf_string);
}

TEST(AppleUtilityTest, ToStringNull) {
  CFStringRef cf_string = nullptr;
  ASSERT_EQ("", Apple::toString(cf_string));
}

TEST(AppleUtilityTest, ToInt) {
  int int_value = 42;
  CFNumberRef cf_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &int_value);
  ASSERT_EQ(int_value, Apple::toInt(cf_number));
}

TEST(AppleUtilityTest, ToIntNull) {
  CFNumberRef cf_number = nullptr;
  ASSERT_EQ(0, Apple::toInt(cf_number));
}

} // namespace Apple
} // namespace Envoy
