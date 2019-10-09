#include "common/stats/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

TEST(UtilityTest, StatSuffix) {
  EXPECT_EQ("items", Utility::suffixedStatsName("items", Histogram::Unit::Unspecified));
  EXPECT_EQ("content_b", Utility::suffixedStatsName("content", Histogram::Unit::Bytes));
  EXPECT_EQ("duration_us", Utility::suffixedStatsName("duration", Histogram::Unit::Microseconds));
  EXPECT_EQ("duration_ms", Utility::suffixedStatsName("duration", Histogram::Unit::Milliseconds));
}

TEST(UtilityTest, UnitSymbols) {
  EXPECT_EQ("", Utility::unitSymbol(Histogram::Unit::Unspecified));
  EXPECT_EQ("b", Utility::unitSymbol(Histogram::Unit::Bytes));
  EXPECT_EQ("us", Utility::unitSymbol(Histogram::Unit::Microseconds));
  EXPECT_EQ("ms", Utility::unitSymbol(Histogram::Unit::Milliseconds));
}

} // namespace Stats
} // namespace Envoy
