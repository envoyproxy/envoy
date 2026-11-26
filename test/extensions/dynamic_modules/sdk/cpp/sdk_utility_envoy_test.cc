#include <string>
#include <string_view>

#include "source/common/http/header_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace DynamicModules {
namespace Utility {

// sdk.h does not build with Envoy's warning flags, so this declares the helpers it tests.
bool isValidHeaderName(std::string_view name);
bool isValidHeaderValue(std::string_view value);

namespace {

TEST(HeaderValidityTest, EveryByteMatchesEnvoy) {
  // The one expected difference: Envoy's check accepts an empty name.
  EXPECT_TRUE(Http::HeaderUtility::headerNameIsValid(""));
  EXPECT_FALSE(isValidHeaderName(""));
  for (int c = 0; c < 256; ++c) {
    const std::string byte(1, static_cast<char>(c));
    for (const std::string& name : {byte, ":" + byte, "x" + byte + "y"}) {
      EXPECT_EQ(isValidHeaderName(name), Http::HeaderUtility::headerNameIsValid(name)) << c;
    }
    // Positions in the vector part of the loop and in its scalar remainder, for several sizes.
    for (const size_t size : {3, 5, 12, 40, 67}) {
      for (const size_t position : {size_t{0}, size / 2, size - 1}) {
        std::string value(size, 'v');
        value[position] = static_cast<char>(c);
        EXPECT_EQ(isValidHeaderValue(value), Http::HeaderUtility::headerValueIsValid(value))
            << c << " at " << position << " of " << size;
      }
    }
  }
}

} // namespace
} // namespace Utility
} // namespace DynamicModules
} // namespace Envoy
