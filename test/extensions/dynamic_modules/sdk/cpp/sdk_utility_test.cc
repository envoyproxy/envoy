#include <string_view>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace DynamicModules {
namespace Utility {
namespace {

TEST(HeaderValidityTest, Names) {
  for (const std::string_view name : {"x-custom", "X-Mixed-Case", ":path", "a!#$%&'*+-.^_`|~0"}) {
    EXPECT_TRUE(isValidHeaderName(name)) << name;
  }
  for (const std::string_view name :
       {std::string_view(""), std::string_view(":"), std::string_view("::path"),
        std::string_view("x:y"), std::string_view("bad name"), std::string_view("x-bad\r"),
        std::string_view("x-bad\n"), std::string_view("x-bad\0", 6),
        std::string_view("caf\xc3\xa9")}) {
    EXPECT_FALSE(isValidHeaderName(name)) << name;
  }
}

TEST(HeaderValidityTest, Values) {
  for (const std::string_view value : {std::string_view(""), std::string_view("value"),
                                       std::string_view("a\tb c"), std::string_view("\x80\xff")}) {
    EXPECT_TRUE(isValidHeaderValue(value)) << value;
  }
  for (const std::string_view value :
       {std::string_view("a\rb"), std::string_view("a\nb"), std::string_view("a\0b", 3),
        std::string_view("bell\a"), std::string_view("del\x7f"),
        std::string_view("value\r\nx-injected: 1")}) {
    EXPECT_FALSE(isValidHeaderValue(value)) << value;
  }
}

} // namespace
} // namespace Utility
} // namespace DynamicModules
} // namespace Envoy
