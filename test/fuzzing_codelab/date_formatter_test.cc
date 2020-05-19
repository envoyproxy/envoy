#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "test/fuzzing_codelab/date_formatter.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Fuzz {

TEST(DateFormatter, FromTime) {
  const SystemTime time1(std::chrono::seconds(1522796769));
  EXPECT_EQ("2018-04-03T23:06:09.000Z", DateFormatter("%Y-%m-%dT%H:%M:%S.000Z").fromTime(time1));
  EXPECT_EQ("aaa23", DateFormatter(std::string(3, 'a') + "%H").fromTime(time1));
  const SystemTime time2(std::chrono::seconds(0));
  EXPECT_EQ("1970-01-01T00:00:00.000Z", DateFormatter("%Y-%m-%dT%H:%M:%S.000Z").fromTime(time2));
  EXPECT_EQ("aaa00", DateFormatter(std::string(3, 'a') + "%H").fromTime(time2));
}

} // namespace Fuzz
} // namespace Envoy
