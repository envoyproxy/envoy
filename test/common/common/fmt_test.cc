#include "common/common/fmt.h"
#include "common/common/logger.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(FormatHelpersTest, Format) {
  absl::string_view sv = "This is my string.";
  absl::string_view my_string = sv.substr(8, 9);
  absl::string_view is = sv.substr(5, 2);

  EXPECT_EQ("it's my string!", fmt::format("it's {}!", my_string));
  EXPECT_EQ("it's my string!", fmt::format("it's {:s}!", my_string));
  EXPECT_EQ("**is**", fmt::format("{:*^6}", is));
}

TEST(FormatHelpersTest, FormatLogMessages) {
  absl::string_view sv = "formatted";
  ENVOY_LOG_MISC(info, "fake {} message", sv);
}

} // namespace Envoy
